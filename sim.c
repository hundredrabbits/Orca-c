#include "gbuffer.h"
#include "mark.h"
#include "sim.h"

//////// Utilities

static Glyph const indexed_glyphs[] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', //  0 - 11
    'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', // 12 - 23
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', // 24 - 35
};

enum { Glyphs_index_max = sizeof indexed_glyphs };

// Always returns 0 through (sizeof indexed_glyphs) - 1, and works on
// capitalized glyphs as well. The index of the lower-cased glyph is returned
// if the glyph is capitalized.
#if 1
// Branchless implementation. Assumes two's complement.
static Usz index_of(Glyph c) {
  int i = c;
  enum {
    // All number chars have this bit set. Some alpha chars do.
    Num_bit = 1 << 4,
    // All alpha chars have this bit set. No number chars do.
    Alpha_bit = 1 << 6,
    // The bits we use from a number char (0000 1111) to get an index number
    Lower_4 = 0xF,
    // The bits we use from an alpha char (0001 1111) to get an index number
    Lower_5 = 0x1F,
  };
  union {
    uint32_t u;
    int32_t i;
  } pui;
  // Turn the alpha bit into a mask of all 32 bits
  pui.u = (uint32_t)(i & Alpha_bit) << UINT32_C(25);
  int alpha_mask = pui.i >> 31;
  // Turn the number bit into a mask of all 32 bits
  pui.u = (uint32_t)(i & Num_bit) << UINT32_C(27);
  int num_mask = pui.i >> 31;
  // If it's an alpha char, we add 9 to it, bringing 'a'/'A' from 1 to 10, 'b'
  // to 11, etc.
  return (Usz)((i & ((alpha_mask & Lower_5) | (num_mask & Lower_4))) +
               (9 & alpha_mask));
  // If the glyph might be a non-valid char in certain ranges (like '^' char)
  // we will return a number here greater than 35. We could do % 36 here if we
  // wanted to be really safe.
}
#else
// Reference implementation
static Usz index_of(Glyph c) {
  if (c == '.')
    return 0;
  if (c >= '0' && c <= '9')
    return (Usz)(c - '0');
  if (c >= 'A' && c <= 'Z')
    return (Usz)(c - 'A' + 10);
  if (c >= 'a' && c <= 'z')
    return (Usz)(c - 'a' + 10);
  return 0;
}
#endif

static inline Glyph glyph_of(Usz index) {
  assert(index < Glyphs_index_max);
  return indexed_glyphs[index];
}

static Glyph glyphs_add(Glyph a, Glyph b) {
  Usz ia = index_of(a);
  Usz ib = index_of(b);
  return indexed_glyphs[(ia + ib) % Glyphs_index_max];
}

static inline bool glyph_is_lowercase(Glyph g) { return g & (1 << 5); }
static inline bool glyph_is_uppercase(Glyph g) { return (g & (1 << 5)) == 0; }
static inline Glyph glyph_lowered_unsafe(Glyph g) {
  return (Glyph)(g | (1 << 5));
}

ORCA_PURE static bool oper_has_neighboring_bang(Glyph const* gbuf, Usz h, Usz w,
                                                Usz y, Usz x) {
  Glyph const* gp = gbuf + w * y + x;
  if (x < w - 1 && gp[1] == '*')
    return true;
  if (x > 0 && *(gp - 1) == '*')
    return true;
  if (y < h - 1 && gp[w] == '*')
    return true;
  // note: negative array subscript on rhs of short-circuit, may cause ub if
  // the arithmetic under/overflows, even if guarded the guard on lhs is false
  if (y > 0 && *(gp - w) == '*')
    return true;
  return false;
}

static U8 midi_note_number_of(Glyph g) {
  switch (g) {
  case 'C':
    return 0;
  case 'c':
    return 1;
  case 'D':
    return 2;
  case 'd':
    return 3;
  case 'E':
    return 4;
  case 'F':
    return 5;
  case 'f':
    return 6;
  case 'G':
    return 7;
  case 'g':
    return 8;
  case 'A':
    return 9;
  case 'a':
    return 10;
  case 'B':
    return 11;
  default:
    return UINT8_MAX;
  }
}

static ORCA_FORCE_NO_INLINE U8 midi_velocity_of(Glyph g) {
  Usz n = index_of(g);
  // scale [0,9] to [0,127]
  if (n < 10)
    return (U8)(n * 14 + 1);
  n -= 10;
  // scale [0,25] to [0,127]
  // js seems to send 1 when original n is < 10, and 0 when n is 11. Is that
  // the intended behavior?
  if (n == 0)
    return UINT8_C(0);
  if (n >= 26)
    return UINT8_C(127);
  return (U8)(n * 5 - 3);
}

typedef struct {
  Bank* bank;
  Usz bank_size;
  Bank_cursor cursor;
  Glyph* vars_slots;
  Piano_bits piano_bits;
  Oevent_list* oevent_list;
} Oper_phase1_extras;

typedef Oper_phase1_extras oper_behavior_extras;

static void oper_bank_store(oper_behavior_extras* extra_params, Usz width,
                            Usz y, Usz x, I32* restrict vals, Usz num_vals) {
  assert(num_vals > 0);
  Usz index = y * width + x;
  assert(index < ORCA_BANK_INDEX_MAX);
  extra_params->bank_size = bank_append(
      extra_params->bank, extra_params->bank_size, index, vals, num_vals);
}
static Usz oper_bank_load(Oper_phase1_extras* extra_params, Usz width, Usz y,
                          Usz x, I32* restrict out_vals, Usz out_count) {
  Usz index = y * width + x;
  assert(index < ORCA_BANK_INDEX_MAX);
  return bank_read(extra_params->bank->data, extra_params->bank_size,
                   &extra_params->cursor, index, out_vals, out_count);
}

static void oper_poke_and_stun(Glyph* restrict gbuffer, Mark* restrict mbuffer,
                               Usz height, Usz width, Usz y, Usz x, Isz delta_y,
                               Isz delta_x, Glyph g) {
  Isz y0 = (Isz)y + delta_y;
  Isz x0 = (Isz)x + delta_x;
  if (y0 < 0 || x0 < 0 || (Usz)y0 >= height || (Usz)x0 >= width)
    return;
  Usz offs = (Usz)y0 * width + (Usz)x0;
  gbuffer[offs] = g;
  mbuffer[offs] |= Mark_flag_sleep;
}

ORCA_FORCE_NO_INLINE static void
oper_copy_columns(Glyph* restrict gbuffer, Mark* restrict mbuffer, Usz height,
                  Usz width, Usz y, Usz x, Isz in_delta_y, Isz in_delta_x,
                  Isz out_delta_y, Isz out_delta_x, Isz count, bool stun) {
  //Isz in_y0 = (Isz)y + in_delta_y;
  //Isz out_y0 = (Isz)y + out_delta_y;
  //if (in_y0 < 0 || (Usz)in_y0 >= height || out_y0 < 0 || (Usz)out_y0 >= height)
  //  return;
  //Glyph* in_row = gbuffer + width * (Usz)in_y0;
  //Glyph* out_row = gbuffer + width * (Usz)out_y0;
  //for (Usz i = 0; i < count; ++i) {
  //  Isz in_x0 = (Isz)x + in_delta_x + i;
  //  Isz out_x0 = (Isz)x + out_delta_x + i;
  //  if (out_x0 < 0 || (Usz)out_x0 >= width) continue;
  //  Glyph g = in_x0 < 0 || (Usz)in_x0 >= width ? '.' : *(in_row + (Usz)in_x0);
  //  out_row[(Usz)out_x0] = g;
  //}
  for (Isz i = 0; i < count; ++i) {
    Glyph g = gbuffer_peek_relative(gbuffer, height, width, y, x, in_delta_y,
                                    in_delta_x + i);
    if (stun) {
      oper_poke_and_stun(gbuffer, mbuffer, height, width, y, x, out_delta_y,
                         out_delta_x + i, g);
    } else {
      gbuffer_poke_relative(gbuffer, height, width, y, x, out_delta_y,
                            out_delta_x + i, g);
    }
  }
}

ORCA_FORCE_STATIC_INLINE
Usz usz_clamp(Usz val, Usz min, Usz max) {
  if (val < min)
    return min;
  if (val > max)
    return max;
  return val;
}

#define OPER_PHASE_COMMON_ARGS                                                 \
  Glyph *const restrict gbuffer, Mark *const restrict mbuffer,                 \
      Usz const height, Usz const width, Usz const y, Usz const x,             \
      Usz Tick_number, oper_behavior_extras *const extra_params,               \
      Mark const cell_flags, Glyph const This_oper_char

#define OPER_IGNORE_COMMON_ARGS()                                              \
  (void)gbuffer;                                                               \
  (void)mbuffer;                                                               \
  (void)height;                                                                \
  (void)width;                                                                 \
  (void)y;                                                                     \
  (void)x;                                                                     \
  (void)Tick_number;                                                           \
  (void)extra_params;                                                          \
  (void)cell_flags;                                                            \
  (void)This_oper_char;

#define OPER_FUNCTION_ATTRIBS ORCA_FORCE_NO_INLINE static void

#define BEGIN_OPERATOR(_oper_name)                                             \
  OPER_FUNCTION_ATTRIBS oper_behavior_##_oper_name(OPER_PHASE_COMMON_ARGS) {   \
    OPER_IGNORE_COMMON_ARGS()

#define END_OPERATOR }

#define PEEK(_delta_y, _delta_x)                                               \
  gbuffer_peek_relative(gbuffer, height, width, y, x, _delta_y, _delta_x)
#define POKE(_delta_y, _delta_x, _glyph)                                       \
  gbuffer_poke_relative(gbuffer, height, width, y, x, _delta_y, _delta_x,      \
                        _glyph)
#define STUN(_delta_y, _delta_x)                                               \
  mbuffer_poke_relative_flags_or(mbuffer, height, width, y, x, _delta_y,       \
                                 _delta_x, Mark_flag_sleep)
#define POKE_STUNNED(_delta_y, _delta_x, _glyph)                               \
  oper_poke_and_stun(gbuffer, mbuffer, height, width, y, x, _delta_y,          \
                     _delta_x, _glyph)
#define LOCK(_delta_y, _delta_x)                                               \
  mbuffer_poke_relative_flags_or(mbuffer, height, width, y, x, _delta_y,       \
                                 _delta_x, Mark_flag_lock)

#define STORE(_i32_array)                                                      \
  oper_bank_store(extra_params, width, y, x, _i32_array,                       \
                  ORCA_ARRAY_COUNTOF(_i32_array))
#define LOAD(_i32_array)                                                       \
  oper_bank_load(extra_params, width, y, x, _i32_array,                        \
                 ORCA_ARRAY_COUNTOF(_i32_array))

#define LEGACY_PHASE_GUARD                                                     \
  if (!IS_AWAKE)                                                               \
  return

#define IN Mark_flag_input
#define OUT Mark_flag_output
#define NONLOCKING Mark_flag_lock
#define HASTE Mark_flag_haste_input

#define REALIZE_DUAL                                                           \
  bool const Dual_is_active =                                                  \
      (glyph_is_uppercase(This_oper_char)) ||                                  \
      oper_has_neighboring_bang(gbuffer, height, width, y, x);

#define BEGIN_PORTS                                                            \
  {                                                                            \
    bool const Oper_ports_enabled = Dual_is_active;

#define BEGIN_ACTIVE_PORTS                                                     \
  {                                                                            \
    bool const Oper_ports_enabled = true;

#define DUAL_IS_ACTIVE Dual_is_active

#define IS_AWAKE (!(cell_flags & (Mark_flag_lock | Mark_flag_sleep)))

#define STOP_IF_DUAL_INACTIVE                                                  \
  if (!Dual_is_active)                                                         \
  return

#define STOP_IF_NOT_BANGED                                                     \
  if (!oper_has_neighboring_bang(gbuffer, height, width, y, x))                \
  return

#define OPER_PORT_IO_MASK                                                      \
  (Mark_flag_input | Mark_flag_output | Mark_flag_haste_input)
#define OPER_PORT_CELL_ENABLING_MASK (Mark_flag_lock | Mark_flag_sleep)
#define OPER_PORT_FLIP_LOCK_BIT(_flags) ((_flags) ^ Mark_flag_lock)

#define PORT(_delta_y, _delta_x, _flags)                                       \
  if (Oper_ports_enabled && !(cell_flags & OPER_PORT_CELL_ENABLING_MASK))      \
  mbuffer_poke_relative_flags_or(mbuffer, height, width, y, x, _delta_y,       \
                                 _delta_x, OPER_PORT_FLIP_LOCK_BIT(_flags))
#define END_PORTS }

//////// Operators

#define ORCA_UNIQUE_OPERATORS(_)                                               \
  _('!', keys)                                                                 \
  _('#', comment)                                                              \
  _('*', bang)                                                                 \
  _(':', midi)                                                                 \
  _('=', osc)

#define ORCA_ALPHA_OPERATORS(_)                                                \
  _('A', add)                                                                  \
  _('B', banger)                                                               \
  _('C', clock)                                                                \
  _('D', delay)                                                                \
  _('E', movement)                                                             \
  _('F', if)                                                                   \
  _('G', generator)                                                            \
  _('H', halt)                                                                 \
  _('I', increment)                                                            \
  _('J', jump)                                                                 \
  _('K', kill)                                                                 \
  _('L', loop)                                                                 \
  _('M', modulo)                                                               \
  _('N', movement)                                                             \
  _('O', offset)                                                               \
  _('P', push)                                                                 \
  _('Q', query)                                                                \
  _('R', random)                                                               \
  _('S', movement)                                                             \
  _('T', track)                                                                \
  _('U', uturn)                                                                \
  _('V', variable)                                                             \
  _('W', movement)                                                             \
  _('X', teleport)                                                             \
  _('Z', zig)

#define MOVEMENT_CASES                                                         \
  'N' : case 'n' : case 'E' : case 'e' : case 'S' : case 's' : case 'W'        \
      : case 'w'

BEGIN_OPERATOR(movement)
  if (cell_flags & (Mark_flag_lock | Mark_flag_sleep))
    return;
  if (glyph_is_lowercase(This_oper_char) &&
      !oper_has_neighboring_bang(gbuffer, height, width, y, x))
    return;

  Isz delta_y, delta_x;

  switch (glyph_lowered_unsafe(This_oper_char)) {
  case 'n':
    delta_y = -1;
    delta_x = 0;
    break;
  case 'e':
    delta_y = 0;
    delta_x = 1;
    break;
  case 's':
    delta_y = 1;
    delta_x = 0;
    break;
  case 'w':
    delta_y = 0;
    delta_x = -1;
    break;
  default:
    // could cause strict aliasing problem, maybe
    delta_y = 0;
    delta_x = 0;
    break;
  }
  Isz y0 = (Isz)y + delta_y;
  Isz x0 = (Isz)x + delta_x;
  if (y0 >= (Isz)height || x0 >= (Isz)width || y0 < 0 || x0 < 0) {
    gbuffer[y * width + x] = '*';
    return;
  }
  Glyph* restrict g_at_dest = gbuffer + (Usz)y0 * width + (Usz)x0;
  if (*g_at_dest == '.') {
    *g_at_dest = This_oper_char;
    gbuffer[y * width + x] = '.';
    mbuffer[(Usz)y0 * width + (Usz)x0] |= Mark_flag_sleep;
  } else {
    gbuffer[y * width + x] = '*';
  }
END_OPERATOR

BEGIN_OPERATOR(keys)
  BEGIN_ACTIVE_PORTS
    PORT(0, 1, IN);
    PORT(1, 0, OUT);
  END_PORTS

  LEGACY_PHASE_GUARD;
  Glyph g = PEEK(0, 1);
  Piano_bits pb = piano_bits_of(g);
  // instead of this extra branch, could maybe just leave output port unlocked
  // so the '*' goes away on its own?
  if (pb == ORCA_PIANO_BITS_NONE)
    return;
  Glyph o;
  if (ORCA_LIKELY((pb & extra_params->piano_bits) == ORCA_PIANO_BITS_NONE))
    o = '.';
  else
    o = '*';
  POKE(1, 0, o);
END_OPERATOR

BEGIN_OPERATOR(comment)
  if (!IS_AWAKE)
    return;
  // restrict probably ok here...
  Glyph const* restrict gline = gbuffer + y * width;
  Mark* restrict mline = mbuffer + y * width;
  Usz max_x = x + 255;
  if (width < max_x)
    max_x = width;
  for (Usz x0 = x + 1; x0 < max_x; ++x0) {
    Glyph g = gline[x0];
    mline[x0] |= (Mark)Mark_flag_lock;
    if (g == '#')
      break;
  }
END_OPERATOR

BEGIN_OPERATOR(bang)
  if (IS_AWAKE) {
    gbuffer_poke(gbuffer, height, width, y, x, '.');
  }
END_OPERATOR

BEGIN_OPERATOR(midi)
  BEGIN_ACTIVE_PORTS
    for (Usz i = 1; i < 6; ++i) {
      PORT(0, (Isz)i, IN);
    }
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_NOT_BANGED;
  Glyph channel_g = PEEK(0, 1);
  Glyph octave_g = PEEK(0, 2);
  Glyph note_g = PEEK(0, 3);
  Glyph velocity_g = PEEK(0, 4);
  Glyph length_g = PEEK(0, 5);
  U8 octave_num = (U8)index_of(octave_g);
  if (octave_num == 0)
    return;
  if (octave_num > 9)
    octave_num = 9;
  U8 note_num = midi_note_number_of(note_g);
  if (note_num == UINT8_MAX)
    return;
  Usz channel_num = index_of(channel_g);
  if (channel_num > 15)
    channel_num = 15;
  Oevent_midi* oe =
      (Oevent_midi*)oevent_list_alloc_item(extra_params->oevent_list);
  oe->oevent_type = (U8)Oevent_type_midi;
  oe->channel = (U8)channel_num;
  oe->octave = (U8)usz_clamp(octave_num, 1, 9);
  oe->note = note_num;
  oe->velocity = midi_velocity_of(velocity_g);
  oe->bar_divisor = (U8)usz_clamp(index_of(length_g), 1, Glyphs_index_max);
END_OPERATOR

BEGIN_OPERATOR(osc)
  BEGIN_ACTIVE_PORTS
    PORT(0, -2, IN | HASTE);
    PORT(0, -1, IN | HASTE);
    Usz len = index_of(PEEK(0, -1)) + 1;
    if (len > Oevent_osc_int_count)
      len = Oevent_osc_int_count;
    for (Usz i = 0; i < len; ++i) {
      PORT(0, (Isz)i + 1, IN);
    }
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_NOT_BANGED;
  Glyph g = PEEK(0, -2);
  if (g != '.') {
    Usz len = index_of(PEEK(0, -1)) + 1;
    if (len > Oevent_osc_int_count)
      len = Oevent_osc_int_count;
    U8 buff[Oevent_osc_int_count];
    for (Usz i = 0; i < len; ++i) {
      buff[i] = (U8)index_of(PEEK(0, (Isz)i + 1));
    }
    Oevent_osc_ints* oe =
        &oevent_list_alloc_item(extra_params->oevent_list)->osc_ints;
    oe->oevent_type = (U8)Oevent_type_osc_ints;
    oe->glyph = g;
    oe->count = (U8)len;
    for (Usz i = 0; i < len; ++i) {
      oe->numbers[i] = buff[i];
    }
  }
END_OPERATOR

BEGIN_OPERATOR(add)
  REALIZE_DUAL;
  BEGIN_PORTS
    PORT(0, 1, IN);
    PORT(0, 2, IN);
    PORT(1, 0, OUT);
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  POKE(1, 0, glyphs_add(PEEK(0, 1), PEEK(0, 2)));
END_OPERATOR

BEGIN_OPERATOR(banger)
  REALIZE_DUAL;
  BEGIN_PORTS
    PORT(0, 1, IN | NONLOCKING);
    PORT(1, 0, OUT);
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  Glyph g = PEEK(0, 1);
  Glyph result;
  switch (g) {
  case '1':
  case '*':
  case MOVEMENT_CASES:
    result = '*';
    break;
  default:
    result = '.';
  }
  POKE(1, 0, result);
END_OPERATOR

BEGIN_OPERATOR(clock)
  REALIZE_DUAL;
  BEGIN_PORTS
    // This is set as haste in js, but not used during .haste(). Mistake?
    // Replicating here anyway.
    PORT(0, -1, IN | HASTE);
    PORT(0, 1, IN);
    PORT(1, 0, OUT);
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  Usz mod_num = index_of(PEEK(0, 1)) + 1;
  Usz rate = index_of(PEEK(0, -1)) + 1;
  Glyph g = glyph_of(Tick_number / rate % mod_num);
  POKE(1, 0, g);
END_OPERATOR

BEGIN_OPERATOR(delay)
  REALIZE_DUAL;
  BEGIN_PORTS
    PORT(0, 1, IN);
    PORT(0, -1, IN | HASTE);
    PORT(1, 0, OUT);
  END_PORTS
  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  Usz offset = index_of(PEEK(0, 1));
  Usz rate = index_of(PEEK(0, -1)) + 1;
  Glyph g = (Tick_number + offset) % rate == 0 ? '*' : '.';
  POKE(1, 0, g);
END_OPERATOR

BEGIN_OPERATOR(if)
  REALIZE_DUAL;
  BEGIN_PORTS
    PORT(0, 1, IN);
    PORT(0, 2, IN);
    PORT(1, 0, OUT);
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  Glyph g0 = PEEK(0, 1);
  Glyph g1 = PEEK(0, 2);
  POKE(1, 0, g0 == g1 ? '*' : '.');
END_OPERATOR

BEGIN_OPERATOR(generator)
  REALIZE_DUAL;
  I32 data[3];
  data[0] = 0; // x
  data[1] = 0; // y
  data[2] = 0; // len
  if (IS_AWAKE && DUAL_IS_ACTIVE) {
    data[0] = (I32)index_of(PEEK(0, -3));
    data[1] = (I32)index_of(PEEK(0, -2));
    data[2] = (I32)index_of(PEEK(0, -1));
    STORE(data);
  }
  BEGIN_PORTS
    PORT(0, -3, IN | HASTE); // x
    PORT(0, -2, IN | HASTE); // y
    PORT(0, -1, IN | HASTE); // len
    I32 out_x = data[0];
    I32 out_y = data[1] + 1;
    I32 len = data[2] + 1;
    // todo direct buffer manip
    for (I32 i = 0; i < len; ++i) {
      PORT(0, i + 1, IN);
      PORT(out_y, out_x + i, OUT | NONLOCKING);
    }
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  if (LOAD(data)) {
    I32 out_x = data[0];
    I32 out_y = data[1] + 1;
    I32 len = data[2] + 1;
    // oper_copy_columns(gbuffer, mbuffer, height, width, y, x, 0, 1, out_y, out_x,
    //                   len, true);
    for (I32 i = 0; i < len; ++i) {
      Glyph g = PEEK(0, i + 1);
      POKE_STUNNED(out_y, out_x + i, g);
    }
  }
END_OPERATOR

BEGIN_OPERATOR(halt)
  REALIZE_DUAL;
  BEGIN_PORTS
    PORT(1, 0, OUT);
  END_PORTS
END_OPERATOR

BEGIN_OPERATOR(increment)
  REALIZE_DUAL;
  BEGIN_PORTS
    PORT(0, 1, IN);
    PORT(0, 2, IN);
    PORT(1, 0, IN | OUT);
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  Usz min = index_of(PEEK(0, 1));
  Usz max = index_of(PEEK(0, 2));
  Usz val = index_of(PEEK(1, 0));
  ++val;
  if (max == 0)
    max = 10;
  if (val >= max)
    val = min;
  POKE(1, 0, glyph_of(val));
END_OPERATOR

BEGIN_OPERATOR(jump)
  REALIZE_DUAL;
  BEGIN_PORTS
    PORT(-1, 0, IN);
    PORT(1, 0, OUT);
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  POKE(1, 0, PEEK(-1, 0));
END_OPERATOR

BEGIN_OPERATOR(kill)
  REALIZE_DUAL;
  BEGIN_PORTS
    PORT(1, 0, OUT | HASTE);
  END_PORTS
  STOP_IF_DUAL_INACTIVE;
  if (IS_AWAKE) {
    POKE(1, 0, '.');
  }
END_OPERATOR

BEGIN_OPERATOR(loop)
  REALIZE_DUAL;
  BEGIN_PORTS
    PORT(0, -1, IN | HASTE);
  END_PORTS
  if (IS_AWAKE && DUAL_IS_ACTIVE) {
    Usz len = index_of(PEEK(0, -1)) + 1;
    I32 len_data[1];
    len_data[0] = (I32)len;
    STORE(len_data);
    if (len > width - x - 1)
      len = width - x - 1;
    Mark* m = mbuffer + y * width + x + 1;
    for (Usz i = 0; i < len; ++i) {
      m[i] |= Mark_flag_lock;
    }
  }

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  I32 len_data[1];
  // todo should at least stun the 1 column if columns is 1
  if (LOAD(len_data) && len_data[0] >= 0) {
    Usz len = (Usz)len_data[0];
    if (len > width - x - 1)
      len = width - x - 1;
    if (len == 0)
      return;
    if (len > 36)
      len = 36;
    Glyph buff[36];
    Glyph* gs = gbuffer + y * width + x + 1;
    Glyph hopped = *gs;
    // ORCA_MEMCPY(buff, gs + 1, len - 1);
    for (Usz i = 0; i < len; ++i) {
      buff[i] = gs[i + 1];
    }
    buff[len - 1] = hopped;
    // ORCA_MEMCPY(gs, buff, len);
    for (Usz i = 0; i < len; ++i) {
      gs[i] = buff[i];
    }
    Mark* m = mbuffer + y * width + x + 1;
    for (Usz i = 0; i < len; ++i) {
      *m |= Mark_flag_sleep;
    }
  }
END_OPERATOR

BEGIN_OPERATOR(modulo)
  REALIZE_DUAL;
  BEGIN_PORTS
    PORT(0, 1, IN);
    PORT(0, 2, IN);
    PORT(1, 0, OUT);
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  Usz ia = index_of(PEEK(0, 1));
  Usz ib = index_of(PEEK(0, 2));
  POKE(1, 0, indexed_glyphs[ib == 0 ? 0 : (ia % ib)]);
END_OPERATOR

BEGIN_OPERATOR(offset)
  REALIZE_DUAL;
  I32 coords[2];
  coords[0] = 0; // y
  coords[1] = 1; // x
  if (IS_AWAKE && DUAL_IS_ACTIVE) {
    coords[0] = (I32)index_of(PEEK(0, -1));
    coords[1] = (I32)index_of(PEEK(0, -2)) + 1;
    STORE(coords);
  }
  BEGIN_PORTS
    PORT(0, -1, IN | HASTE);
    PORT(0, -2, IN | HASTE);
    PORT(coords[0], coords[1], IN);
    PORT(1, 0, OUT);
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  if (!LOAD(coords)) {
    coords[0] = 0;
    coords[1] = 1;
  }
  POKE(1, 0, PEEK(coords[0], coords[1]));
END_OPERATOR

BEGIN_OPERATOR(push)
  REALIZE_DUAL;
  I32 write_val_x[1];
  write_val_x[0] = 0;
  if (IS_AWAKE && DUAL_IS_ACTIVE) {
    Usz len = index_of(PEEK(0, -1)) + 1;
    Usz key = index_of(PEEK(0, -2));
    write_val_x[0] = (I32)(key % len);
    STORE(write_val_x);
    for (Usz i = 0; i < len; ++i) {
      LOCK(1, (Isz)i);
    }
  }
  BEGIN_PORTS
    PORT(0, -1, IN | HASTE);
    PORT(0, -2, IN | HASTE);
    PORT(0, 1, IN);
    PORT(1, (Isz)write_val_x, OUT);
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  if (!LOAD(write_val_x)) {
    write_val_x[0] = 0;
  }
  POKE(1, write_val_x[0], PEEK(0, 1));
END_OPERATOR

BEGIN_OPERATOR(query)
  REALIZE_DUAL;
  I32 data[3];
  data[0] = 0; // x
  data[1] = 0; // y
  data[2] = 0; // len
  if (IS_AWAKE && DUAL_IS_ACTIVE) {
    data[0] = (I32)index_of(PEEK(0, -3));
    data[1] = (I32)index_of(PEEK(0, -2));
    data[2] = (I32)index_of(PEEK(0, -1));
    STORE(data);
  }
  BEGIN_PORTS
    PORT(0, -3, IN | HASTE); // x
    PORT(0, -2, IN | HASTE); // y
    PORT(0, -1, IN | HASTE); // len
    I32 in_x = data[0] + 1;
    I32 in_y = data[1];
    I32 len = data[2] + 1;
    I32 out_x = 1 - len;
    // todo direct buffer manip
    for (I32 i = 0; i < len; ++i) {
      PORT(in_y, in_x + i, IN);
      PORT(1, out_x + i, OUT);
    }
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  if (LOAD(data)) {
    I32 in_x = data[0] + 1;
    I32 in_y = data[1];
    I32 len = data[2] + 1;
    I32 out_x = 1 - len;
    oper_copy_columns(gbuffer, mbuffer, height, width, y, x, in_y, in_x, 1,
                      out_x, len, false);
    // for (I32 i = 0; i < len; ++i) {
    //   Glyph g = PEEK(in_y, in_x + i);
    //   POKE(1, out_x + i, g);
    // }
  }
END_OPERATOR

static Usz hash32_shift_mult(Usz key) {
  Usz c2 = UINT32_C(0x27d4eb2d);
  key = (key ^ UINT32_C(61)) ^ (key >> UINT32_C(16));
  key = key + (key << UINT32_C(3));
  key = key ^ (key >> UINT32_C(4));
  key = key * c2;
  key = key ^ (key >> UINT32_C(15));
  return key;
}

BEGIN_OPERATOR(random)
  REALIZE_DUAL;
  BEGIN_PORTS
    PORT(0, 1, IN);
    PORT(0, 2, IN);
    PORT(1, 0, OUT);
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  Usz a = index_of(PEEK(0, 1));
  Usz b = index_of(PEEK(0, 2));
  Usz min, max;
  if (a == b) {
    POKE(1, 0, glyph_of(a));
    return;
  } else if (a < b) {
    min = a;
    max = b;
  } else {
    min = b;
    max = a;
  }
  Usz key = y * width + x;
  key = hash32_shift_mult((y * width + x) ^ (Tick_number << UINT32_C(16)));
  Usz val = key % (max + 1 - min) + min;
  POKE(1, 0, glyph_of(val));
END_OPERATOR

BEGIN_OPERATOR(track)
  REALIZE_DUAL;
  Isz read_val_x = 1;
  if (IS_AWAKE) {
    Usz len = index_of(PEEK(0, -1)) + 1;
    Usz key = index_of(PEEK(0, -2));
    read_val_x = (Isz)(key % len) + 1;
    I32 ival[1];
    ival[0] = (I32)read_val_x;
    STORE(ival);
    for (Usz i = 0; i < len; ++i) {
      LOCK(0, (Isz)(i + 1));
    }
  }
  BEGIN_PORTS
    PORT(0, -1, IN | HASTE);
    PORT(0, -2, IN | HASTE);
    PORT(0, (Isz)read_val_x, IN);
    PORT(1, 0, OUT);
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  I32 ival[1];
  if (!LOAD(ival)) {
    ival[0] = 1;
  }
  POKE(1, 0, PEEK(0, ival[0]));
END_OPERATOR

static Isz const uturn_data[] = {
    // clang-format off
  -1,  0, (Isz)'N',
   0, -1, (Isz)'W',
   0,  1, (Isz)'E',
   1,  0, (Isz)'S',
    // clang-format on
};

enum {
  Uturn_per = 3,
  Uturn_loop_limit = Uturn_per * 4,
};

BEGIN_OPERATOR(uturn)
  REALIZE_DUAL;
  BEGIN_PORTS
    for (Usz i = 0; i < Uturn_loop_limit; i += Uturn_per) {
      PORT(uturn_data[i + 0], uturn_data[i + 1], IN | OUT | HASTE | NONLOCKING);
    }
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  for (Usz i = 0; i < Uturn_loop_limit; i += Uturn_per) {
    Isz dy = uturn_data[i + 0];
    Isz dx = uturn_data[i + 1];
    Glyph g = PEEK(dy, dx);
    switch (g) {
    case MOVEMENT_CASES:
      POKE(dy, dx, (Glyph)uturn_data[i + 2]);
    }
  }
END_OPERATOR

BEGIN_OPERATOR(variable)
  REALIZE_DUAL;
  BEGIN_PORTS
    PORT(0, -1, IN | HASTE);
    PORT(0, 1, IN);
    PORT(1, 0, OUT);
  END_PORTS
  if (IS_AWAKE && DUAL_IS_ACTIVE) {
    Glyph left = PEEK(0, -1);
    Usz var_idx;
    if (left >= 'A' && left <= 'Z') {
      var_idx = (Usz)('Z' - left);
    } else if (left >= 'a' && left <= 'z') {
      var_idx = (Usz)(('Z' - 'A') + ('z' - left) + 1);
    } else {
      return;
    }
    Glyph right = PEEK(0, 1);
    if (right == '.')
      return;
    extra_params->vars_slots[var_idx] = right;
  }

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  Glyph left = PEEK(0, -1);
  if (left != '.')
    return;
  Glyph right = PEEK(0, 1);
  Usz var_idx;
  if (right >= 'A' && right <= 'Z') {
    var_idx = (Usz)('Z' - right);
  } else if (right >= 'a' && right <= 'z') {
    var_idx = (Usz)(('Z' - 'A') + ('z' - right) + 1);
  } else {
    return;
  }
  Glyph result = extra_params->vars_slots[var_idx];
  if (result == '.')
    return;
  POKE(1, 0, result);
END_OPERATOR

BEGIN_OPERATOR(teleport)
  REALIZE_DUAL;
  I32 coords[2];
  coords[0] = 1; // y
  coords[1] = 0; // x
  if (IS_AWAKE) {
    coords[0] = (I32)index_of(PEEK(0, -1)) + 1;
    coords[1] = (I32)index_of(PEEK(0, -2));
    STORE(coords);
  }
  BEGIN_PORTS
    PORT(0, -1, IN | HASTE); // y
    PORT(0, -2, IN | HASTE); // x
    PORT(0, 1, IN);
    PORT(coords[0], coords[1], OUT | NONLOCKING);
  END_PORTS

  LEGACY_PHASE_GUARD;
  STOP_IF_DUAL_INACTIVE;
  if (!LOAD(coords)) {
    coords[0] = 1;
    coords[1] = 0;
  }
  POKE_STUNNED(coords[0], coords[1], PEEK(0, 1));
END_OPERATOR

BEGIN_OPERATOR(zig)
  if (!IS_AWAKE)
    return;
  REALIZE_DUAL;
  if (!DUAL_IS_ACTIVE)
    return;
  Glyph* gline = gbuffer + width * y;
  gline[x] = '.';
  if (x + 1 == width)
    return;
  if (gline[x + 1] == '.') {
    gline[x + 1] = This_oper_char;
    mbuffer[width * y + x + 1] |= (U8)Mark_flag_sleep;
  } else {
    Usz n = 256;
    if (x < n)
      n = x;
    for (Usz i = 0; i < n; ++i) {
      if (gline[x - i - 1] != '.') {
        gline[x - i] = This_oper_char;
        break;
      }
    }
  }
END_OPERATOR

//////// Run simulation

#define SIM_EXPAND_UNIQUE(_oper_char, _oper_name)                              \
  case _oper_char:                                                             \
    oper_behavior_##_oper_name(gbuf, mbuf, height, width, iy, ix, tick_number, \
                               &extras, cell_flags, glyph_char);               \
    break;

#define SIM_EXPAND_ALPHA(_upper_oper_char, _oper_name)                         \
  case _upper_oper_char:                                                       \
  case ((char)(_upper_oper_char | (1 << 5))):                                  \
    oper_behavior_##_oper_name(gbuf, mbuf, height, width, iy, ix, tick_number, \
                               &extras, cell_flags, glyph_char);               \
    break;

void orca_run(Gbuffer gbuf, Mbuffer mbuf, Usz height, Usz width,
              Usz tick_number, Bank* bank, Oevent_list* oevent_list,
              Piano_bits piano_bits) {
  Glyph vars_slots[('Z' - 'A' + 1) + ('z' - 'a' + 1)];
  memset(vars_slots, '.', sizeof(vars_slots));
  mbuffer_clear(mbuf, height, width);
  oevent_list_clear(oevent_list);
  Oper_phase1_extras extras;
  extras.bank = bank;
  extras.bank_size = 0;
  bank_cursor_reset(&extras.cursor);
  extras.vars_slots = &vars_slots[0];
  extras.piano_bits = piano_bits;
  extras.oevent_list = oevent_list;

  for (Usz iy = 0; iy < height; ++iy) {
    Glyph const* glyph_row = gbuf + iy * width;
    Mark const* mark_row = mbuf + iy * width;
    for (Usz ix = 0; ix < width; ++ix) {
      Glyph glyph_char = glyph_row[ix];
      if (ORCA_LIKELY(glyph_char == '.'))
        continue;
      Mark cell_flags = mark_row[ix] & (Mark_flag_lock | Mark_flag_sleep);
      switch (glyph_char) {
        ORCA_UNIQUE_OPERATORS(SIM_EXPAND_UNIQUE)
        ORCA_ALPHA_OPERATORS(SIM_EXPAND_ALPHA)
        break;
      }
    }
  }
}
