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

ORCA_FORCE_NO_INLINE
static void oper_movement_phase0(Gbuffer gbuf, Mbuffer mbuf, Usz const height,
                                 Usz const width, Usz const y, Usz const x,
                                 Mark const cell_flags,
                                 Glyph const uppercase_char,
                                 Glyph const actual_char, Isz const delta_y,
                                 Isz const delta_x) {
  if (cell_flags & (Mark_flag_lock | Mark_flag_sleep))
    return;
  if ((actual_char != uppercase_char) &&
      !oper_has_neighboring_bang(gbuf, height, width, y, x))
    return;
  Isz y0 = (Isz)y + delta_y;
  Isz x0 = (Isz)x + delta_x;
  if (y0 >= (Isz)height || x0 >= (Isz)width || y0 < 0 || x0 < 0) {
    gbuf[y * width + x] = '*';
    return;
  }
  Glyph* restrict g_at_dest = gbuf + (Usz)y0 * width + (Usz)x0;
  if (*g_at_dest == '.') {
    *g_at_dest = actual_char;
    gbuf[y * width + x] = '.';
    mbuf[(Usz)y0 * width + (Usz)x0] |= Mark_flag_sleep;
  } else {
    gbuf[y * width + x] = '*';
  }
}

typedef struct {
  Bank* bank;
  Usz bank_size;
  Glyph* vars_slots;
} Oper_phase0_extras;

typedef struct {
  Bank* bank;
  Usz bank_size;
  Bank_cursor cursor;
  Glyph const* vars_slots;
  Piano_bits piano_bits;
  Oevent_list* oevent_list;
} Oper_phase1_extras;

static void oper_bank_store(Oper_phase0_extras* extra_params, Usz width, Usz y,
                            Usz x, I32* restrict vals, Usz num_vals) {
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
                               Usz height, Usz width, Usz x, Usz y, Isz delta_y,
                               Isz delta_x, Glyph g) {
  Isz y0 = (Isz)y + delta_y;
  Isz x0 = (Isz)x + delta_x;
  if (y0 < 0 || x0 < 0 || (Usz)y0 >= height || (Usz)x0 >= width)
    return;
  Usz offs = (Usz)y0 * width + (Usz)x0;
  gbuffer[offs] = g;
  mbuffer[offs] |= Mark_flag_sleep;
}

ORCA_FORCE_STATIC_INLINE
Usz usz_clamp(Usz val, Usz min, Usz max) {
  if (val < min)
    return min;
  if (val > max)
    return max;
  return val;
}

#define ORCA_EXPAND_SOLO_OPER_CHARS(_oper_char, _oper_name)                    \
  Orca_oper_char_##_oper_name = _oper_char,
#define ORCA_EXPAND_DUAL_OPER_CHARS(_upper_oper_char, _lower_oper_char,        \
                                    _oper_name)                                \
  Orca_oper_upper_char_##_oper_name = _upper_oper_char,                        \
  Orca_oper_lower_char_##_oper_name = _lower_oper_char,
#define ORCA_EXPAND_MOVM_OPER_CHARS(_upper_oper_char, _lower_oper_char,        \
                                    _oper_name, _delta_y, _delta_x)            \
  Orca_oper_upper_char_##_oper_name = _upper_oper_char,                        \
  Orca_oper_lower_char_##_oper_name = _lower_oper_char,
#define ORCA_DEFINE_OPER_CHARS(_solo_defs, _dual_defs, _movm_defs)             \
  enum Orca_oper_chars {                                                       \
    _solo_defs(ORCA_EXPAND_SOLO_OPER_CHARS)                                    \
        _dual_defs(ORCA_EXPAND_DUAL_OPER_CHARS)                                \
            _movm_defs(ORCA_EXPAND_MOVM_OPER_CHARS)                            \
  };
#define ORCA_DECLARE_OPERATORS(_solo_defs, _dual_defs, _movm_defs)             \
  ORCA_DEFINE_OPER_CHARS(_solo_defs, _dual_defs, _movm_defs)

#define OPER_PHASE_COMMON_ARGS                                                 \
  Glyph *const restrict gbuffer, Mark *const restrict mbuffer,                 \
      Usz const height, Usz const width, Usz const y, Usz const x,             \
      Usz Tick_number
#define OPER_PHASE_0_COMMON_ARGS                                               \
  OPER_PHASE_COMMON_ARGS, Oper_phase0_extras *const extra_params,              \
      Mark const cell_flags
#define OPER_PHASE_1_COMMON_ARGS                                               \
  OPER_PHASE_COMMON_ARGS, Oper_phase1_extras* const extra_params

#define OPER_IGNORE_COMMON_ARGS()                                              \
  (void)gbuffer;                                                               \
  (void)mbuffer;                                                               \
  (void)height;                                                                \
  (void)width;                                                                 \
  (void)y;                                                                     \
  (void)x;                                                                     \
  (void)Tick_number;                                                           \
  (void)extra_params;

#define OPER_PHASE_SPEC ORCA_FORCE_NO_INLINE static

#define BEGIN_SOLO_PHASE_0(_oper_name)                                         \
  OPER_PHASE_SPEC void oper_phase0_##_oper_name(OPER_PHASE_0_COMMON_ARGS) {    \
    OPER_IGNORE_COMMON_ARGS()                                                  \
    (void)cell_flags;                                                          \
    enum { This_oper_char = Orca_oper_char_##_oper_name };
#define BEGIN_SOLO_PHASE_1(_oper_name)                                         \
  OPER_PHASE_SPEC void oper_phase1_##_oper_name(OPER_PHASE_1_COMMON_ARGS) {    \
    OPER_IGNORE_COMMON_ARGS()                                                  \
    enum { This_oper_char = Orca_oper_char_##_oper_name };
#define BEGIN_DUAL_PHASE_0(_oper_name)                                         \
  OPER_PHASE_SPEC void oper_phase0_##_oper_name(OPER_PHASE_0_COMMON_ARGS,      \
                                                Glyph const This_oper_char) {  \
    OPER_IGNORE_COMMON_ARGS()                                                  \
    (void)cell_flags;                                                          \
    (void)This_oper_char;                                                      \
    enum { Uppercase_oper_char = Orca_oper_upper_char_##_oper_name };          \
    (void)Uppercase_oper_char;
#define BEGIN_DUAL_PHASE_1(_oper_name)                                         \
  OPER_PHASE_SPEC void oper_phase1_##_oper_name(OPER_PHASE_1_COMMON_ARGS,      \
                                                Glyph const This_oper_char) {  \
    OPER_IGNORE_COMMON_ARGS()                                                  \
    (void)This_oper_char;                                                      \
    enum { Uppercase_oper_char = Orca_oper_upper_char_##_oper_name };          \
    (void)Uppercase_oper_char;

#define END_PHASE }

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

#define IN Mark_flag_input
#define OUT Mark_flag_output
#define NONLOCKING Mark_flag_lock
#define HASTE Mark_flag_haste_input

#define REALIZE_DUAL                                                           \
  bool const Dual_is_active =                                                  \
      (Uppercase_oper_char == This_oper_char) ||                               \
      oper_has_neighboring_bang(gbuffer, height, width, y, x);

#define PSEUDO_DUAL bool const Dual_is_active = true

#define BEGIN_DUAL_PORTS                                                       \
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

#define ORCA_SOLO_OPERATORS(_)                                                 \
  _('!', keys)                                                                 \
  _('#', comment)                                                              \
  _('*', bang)                                                                 \
  _(':', midi)

#define ORCA_DUAL_OPERATORS(_)                                                 \
  _('A', 'a', add)                                                             \
  _('B', 'b', banger)                                                          \
  _('C', 'c', clock)                                                           \
  _('D', 'd', delay)                                                           \
  _('F', 'f', if)                                                              \
  _('G', 'g', generator)                                                       \
  _('H', 'h', halt)                                                            \
  _('I', 'i', increment)                                                       \
  _('J', 'j', jump)                                                            \
  _('K', 'k', kill)                                                            \
  _('L', 'l', loop)                                                            \
  _('M', 'm', modulo)                                                          \
  _('O', 'o', offset)                                                          \
  _('P', 'p', push)                                                            \
  _('Q', 'q', query)                                                           \
  _('R', 'r', random)                                                          \
  _('T', 't', track)                                                           \
  _('U', 'u', uturn)                                                           \
  _('V', 'v', variable)                                                        \
  _('X', 'x', teleport)

#define ORCA_MOVEMENT_OPERATORS(_)                                             \
  _('N', 'n', north, -1, 0)                                                    \
  _('E', 'e', east, 0, 1)                                                      \
  _('S', 's', south, 1, 0)                                                     \
  _('W', 'w', west, 0, -1)                                                     \
  _('Z', 'z', southeast, 1, 1)

ORCA_DECLARE_OPERATORS(ORCA_SOLO_OPERATORS, ORCA_DUAL_OPERATORS,
                       ORCA_MOVEMENT_OPERATORS)

#define MOVEMENT_CASES                                                         \
  'N' : case 'n' : case 'E' : case 'e' : case 'S' : case 's' : case 'W'        \
      : case 'w' : case 'Z' : case 'z'

BEGIN_SOLO_PHASE_0(keys)
  BEGIN_ACTIVE_PORTS
    PORT(0, 1, IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_SOLO_PHASE_1(keys)
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
END_PHASE

BEGIN_SOLO_PHASE_0(comment)
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
END_PHASE
BEGIN_SOLO_PHASE_1(comment)
END_PHASE

BEGIN_SOLO_PHASE_0(bang)
  if (IS_AWAKE) {
    gbuffer_poke(gbuffer, height, width, y, x, '.');
  }
END_PHASE
BEGIN_SOLO_PHASE_1(bang)
END_PHASE

BEGIN_SOLO_PHASE_0(midi)
  BEGIN_ACTIVE_PORTS
    for (Usz i = 1; i < 6; ++i) {
      PORT(0, (Isz)i, IN);
    }
  END_PORTS
END_PHASE
BEGIN_SOLO_PHASE_1(midi)
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
END_PHASE

BEGIN_DUAL_PHASE_0(add)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, 1, IN);
    PORT(0, 2, IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(add)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  POKE(1, 0, glyphs_add(PEEK(0, 1), PEEK(0, 2)));
END_PHASE

BEGIN_DUAL_PHASE_0(banger)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, 1, IN | NONLOCKING);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(banger)
  REALIZE_DUAL;
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
END_PHASE

BEGIN_DUAL_PHASE_0(clock)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    // This is set as haste in js, but not used during .haste(). Mistake?
    // Replicating here anyway.
    PORT(0, -1, IN | HASTE);
    PORT(0, 1, IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(clock)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  Usz mod_num = index_of(PEEK(0, 1));
  if (mod_num == 0)
    mod_num = 10;
  Usz rate = usz_clamp(index_of(PEEK(0, -1)), 1, 16);
  Glyph g = glyph_of(Tick_number / rate % mod_num);
  POKE(1, 0, g);
END_PHASE

BEGIN_DUAL_PHASE_0(delay)
  PSEUDO_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, 1, IN);
    PORT(0, -1, IN | HASTE);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(delay)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  Usz offset = index_of(PEEK(0, 1));
  Usz rate = usz_clamp(index_of(PEEK(0, -1)), 2, 16);
  Glyph g = (Tick_number + offset) % rate == 0 ? '*' : '.';
  POKE(1, 0, g);
END_PHASE

BEGIN_DUAL_PHASE_0(if)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, 1, IN);
    PORT(0, 2, IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(if)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  Glyph g0 = PEEK(0, 1);
  Glyph g1 = PEEK(0, 2);
  POKE(1, 0, g0 == g1 ? '*' : '.');
END_PHASE

BEGIN_DUAL_PHASE_0(generator)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, 1, IN);
    PORT(1, 0, OUT | NONLOCKING);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(generator)
  STOP_IF_NOT_BANGED;
  POKE_STUNNED(1, 0, PEEK(0, 1));
END_PHASE

BEGIN_DUAL_PHASE_0(halt)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(halt)
END_PHASE

BEGIN_DUAL_PHASE_0(increment)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, 1, IN);
    PORT(0, 2, IN);
    PORT(1, 0, IN | OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(increment)
  REALIZE_DUAL;
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
END_PHASE

BEGIN_DUAL_PHASE_0(jump)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(-1, 0, IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(jump)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  POKE(1, 0, PEEK(-1, 0));
END_PHASE

BEGIN_DUAL_PHASE_0(kill)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(1, 0, OUT | HASTE);
  END_PORTS
  STOP_IF_DUAL_INACTIVE;
  if (IS_AWAKE) {
    POKE(1, 0, '.');
  }
END_PHASE
BEGIN_DUAL_PHASE_1(kill)
END_PHASE

BEGIN_DUAL_PHASE_0(loop)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, -1, IN | HASTE);
  END_PORTS
  if (IS_AWAKE && DUAL_IS_ACTIVE) {
    Usz len = index_of(PEEK(0, -1));
    I32 len_data[1];
    len_data[0] = (I32)len;
    STORE(len_data);
    if (len == 0)
      len = 1;
    else if (len > 16)
      len = 16;
    if (len > width - x - 1)
      len = width - x - 1;
    Mark* m = mbuffer + y * width + x + 1;
    for (Usz i = 0; i < len; ++i) {
      m[i] |= Mark_flag_lock;
    }
  }
END_PHASE
BEGIN_DUAL_PHASE_1(loop)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  I32 len_data[1];
  // todo should at least stun the 1 column if columns is 1
  if (LOAD(len_data) && len_data[0] >= 0) {
    Usz len = (Usz)len_data[0];
    if (len > width - x - 1)
      len = width - x - 1;
    if (len == 0)
      return;
    if (len > 16)
      len = 16;
    Glyph buff[16];
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
END_PHASE

BEGIN_DUAL_PHASE_0(modulo)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, 1, IN);
    PORT(0, 2, IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(modulo)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  Usz ia = index_of(PEEK(0, 1));
  Usz ib = index_of(PEEK(0, 2));
  POKE(1, 0, indexed_glyphs[ib == 0 ? 0 : (ia % ib)]);
END_PHASE

BEGIN_DUAL_PHASE_0(offset)
  REALIZE_DUAL;
  I32 coords[2];
  coords[0] = 0; // y
  coords[1] = 1; // x
  if (IS_AWAKE && DUAL_IS_ACTIVE) {
    coords[0] = (I32)usz_clamp(index_of(PEEK(0, -1)), 0, 16);
    coords[1] = (I32)usz_clamp(index_of(PEEK(0, -2)) + 1, 1, 16);
    STORE(coords);
  }
  BEGIN_DUAL_PORTS
    PORT(0, -1, IN | HASTE);
    PORT(0, -2, IN | HASTE);
    PORT(coords[0], coords[1], IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(offset)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  I32 coords[2];
  if (!LOAD(coords)) {
    coords[0] = 0;
    coords[1] = 1;
  }
  POKE(1, 0, PEEK(coords[0], coords[1]));
END_PHASE

BEGIN_DUAL_PHASE_0(push)
  REALIZE_DUAL;
  I32 write_val_x[1];
  write_val_x[0] = 0;
  if (IS_AWAKE && DUAL_IS_ACTIVE) {
    Usz len = usz_clamp(index_of(PEEK(0, -1)), 1, 16);
    Usz key = index_of(PEEK(0, -2));
    write_val_x[0] = (I32)(key % len);
    STORE(write_val_x);
    for (Isz i = 0; i < write_val_x[0]; ++i) {
      LOCK(1, i);
    }
  }
  BEGIN_DUAL_PORTS
    PORT(0, -1, IN | HASTE);
    PORT(0, -2, IN | HASTE);
    PORT(0, 1, IN);
    PORT(1, (Isz)write_val_x, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(push)
  STOP_IF_NOT_BANGED;
  I32 write_val_x[1];
  if (!LOAD(write_val_x)) {
    write_val_x[0] = 0;
  }
  POKE(1, write_val_x[0], PEEK(0, 1));
END_PHASE

BEGIN_DUAL_PHASE_0(query)
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
  BEGIN_DUAL_PORTS
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
    }
    for (I32 i = 0; i < len; ++i) {
      PORT(1, out_x + i, OUT);
    }
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(query)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  I32 data[3];
  if (LOAD(data)) {
    I32 in_x = data[0] + 1;
    I32 in_y = data[1];
    I32 len = data[2] + 1;
    I32 out_x = 1 - len;
    for (I32 i = 0; i < len; ++i) {
      Glyph g = PEEK(in_y, in_x + i);
      POKE(1, out_x + i, g);
    }
  }
END_PHASE

static Usz hash32_shift_mult(Usz key) {
  Usz c2 = UINT32_C(0x27d4eb2d);
  key = (key ^ UINT32_C(61)) ^ (key >> UINT32_C(16));
  key = key + (key << UINT32_C(3));
  key = key ^ (key >> UINT32_C(4));
  key = key * c2;
  key = key ^ (key >> UINT32_C(15));
  return key;
}

BEGIN_DUAL_PHASE_0(random)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, 1, IN);
    PORT(0, 2, IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(random)
  REALIZE_DUAL;
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
  Usz val = key % (max - min) + min;
  POKE(1, 0, glyph_of(val));
END_PHASE

BEGIN_DUAL_PHASE_0(track)
  PSEUDO_DUAL;
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
  BEGIN_DUAL_PORTS
    PORT(0, -1, IN | HASTE);
    PORT(0, -2, IN | HASTE);
    PORT(0, (Isz)read_val_x, IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(track)
  I32 ival[1];
  if (!LOAD(ival)) {
    ival[0] = 1;
  }
  POKE(1, 0, PEEK(0, ival[0]));
END_PHASE

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

BEGIN_DUAL_PHASE_0(uturn)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    for (Usz i = 0; i < Uturn_loop_limit; i += Uturn_per) {
      PORT(uturn_data[i + 0], uturn_data[i + 1], IN | OUT | HASTE | NONLOCKING);
    }
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(uturn)
  REALIZE_DUAL;
  if (!DUAL_IS_ACTIVE)
    return;
  for (Usz i = 0; i < Uturn_loop_limit; i += Uturn_per) {
    Isz dy = uturn_data[i + 0];
    Isz dx = uturn_data[i + 1];
    Glyph g = PEEK(dy, dx);
    switch (g) {
    case MOVEMENT_CASES:
      POKE(dy, dx, (Glyph)uturn_data[i + 2]);
    }
  }
END_PHASE

BEGIN_DUAL_PHASE_0(variable)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
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
END_PHASE
BEGIN_DUAL_PHASE_1(variable)
  REALIZE_DUAL;
  if (!DUAL_IS_ACTIVE)
    return;
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
END_PHASE

BEGIN_DUAL_PHASE_0(teleport)
  PSEUDO_DUAL;
  I32 coords[2];
  coords[0] = 1; // y
  coords[1] = 0; // x
  if (IS_AWAKE) {
    coords[0] = (I32)usz_clamp(index_of(PEEK(0, -1)), 1, 16);
    coords[1] = (I32)usz_clamp(index_of(PEEK(0, -2)), 0, 16);
    STORE(coords);
  }
  BEGIN_DUAL_PORTS
    PORT(0, -1, IN | HASTE); // y
    PORT(0, -2, IN | HASTE); // x
    PORT(0, 1, IN);
    PORT(coords[0], coords[1], OUT | NONLOCKING);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(teleport)
  I32 coords[2];
  if (!LOAD(coords)) {
    coords[0] = 1;
    coords[1] = 0;
  }
  POKE_STUNNED(coords[0], coords[1], PEEK(0, 1));
END_PHASE

//////// Run simulation

#define SIM_EXPAND_SOLO_PHASE_0(_oper_char, _oper_name)                        \
  case _oper_char:                                                             \
    oper_phase0_##_oper_name(gbuf, mbuf, height, width, iy, ix, tick_number,   \
                             extra_params, cell_flags);                        \
    break;
#define SIM_EXPAND_SOLO_PHASE_1(_oper_char, _oper_name)                        \
  case _oper_char:                                                             \
    oper_phase1_##_oper_name(gbuf, mbuf, height, width, iy, ix, tick_number,   \
                             extra_params);                                    \
    break;

#define SIM_EXPAND_DUAL_PHASE_0(_upper_oper_char, _lower_oper_char,            \
                                _oper_name)                                    \
  case _upper_oper_char:                                                       \
  case _lower_oper_char:                                                       \
    oper_phase0_##_oper_name(gbuf, mbuf, height, width, iy, ix, tick_number,   \
                             extra_params, cell_flags, glyph_char);            \
    break;
#define SIM_EXPAND_DUAL_PHASE_1(_upper_oper_char, _lower_oper_char,            \
                                _oper_name)                                    \
  case _upper_oper_char:                                                       \
  case _lower_oper_char:                                                       \
    oper_phase1_##_oper_name(gbuf, mbuf, height, width, iy, ix, tick_number,   \
                             extra_params, glyph_char);                        \
    break;

#define SIM_EXPAND_MOVM_PHASE_0(_upper_oper_char, _lower_oper_char,            \
                                _oper_name, _delta_y, _delta_x)                \
  case _upper_oper_char:                                                       \
  case _lower_oper_char:                                                       \
    oper_movement_phase0(gbuf, mbuf, height, width, iy, ix, cell_flags,        \
                         _upper_oper_char, glyph_char, _delta_y, _delta_x);    \
    break;

static void sim_phase_0(Gbuffer gbuf, Mbuffer mbuf, Usz height, Usz width,
                        Usz tick_number, Oper_phase0_extras* extra_params) {
  for (Usz iy = 0; iy < height; ++iy) {
    Glyph const* glyph_row = gbuf + iy * width;
    Mark const* mark_row = mbuf + iy * width;
    for (Usz ix = 0; ix < width; ++ix) {
      Glyph glyph_char = glyph_row[ix];
      if (ORCA_LIKELY(glyph_char == '.'))
        continue;
      Mark cell_flags = mark_row[ix] & (Mark_flag_lock | Mark_flag_sleep);
      switch (glyph_char) {
        ORCA_SOLO_OPERATORS(SIM_EXPAND_SOLO_PHASE_0)
        ORCA_DUAL_OPERATORS(SIM_EXPAND_DUAL_PHASE_0)
        ORCA_MOVEMENT_OPERATORS(SIM_EXPAND_MOVM_PHASE_0)
      }
    }
  }
}

static void sim_phase_1(Gbuffer gbuf, Mbuffer mbuf, Usz height, Usz width,
                        Usz tick_number, Oper_phase1_extras* extra_params) {
  for (Usz iy = 0; iy < height; ++iy) {
    Glyph const* glyph_row = gbuf + iy * width;
    Mark const* mark_row = mbuf + iy * width;
    for (Usz ix = 0; ix < width; ++ix) {
      Glyph glyph_char = glyph_row[ix];
      if (ORCA_LIKELY(glyph_char == '.'))
        continue;
      if (mark_row[ix] & (Mark_flag_lock | Mark_flag_sleep))
        continue;
      switch (glyph_char) {
        ORCA_SOLO_OPERATORS(SIM_EXPAND_SOLO_PHASE_1)
        ORCA_DUAL_OPERATORS(SIM_EXPAND_DUAL_PHASE_1)
      }
    }
  }
}

void orca_run(Gbuffer gbuf, Mbuffer mbuf, Usz height, Usz width,
              Usz tick_number, Bank* bank, Oevent_list* oevent_list,
              Piano_bits piano_bits) {
  Glyph vars_slots[('Z' - 'A' + 1) + ('z' - 'a' + 1)];
  memset(vars_slots, '.', sizeof(vars_slots));
  mbuffer_clear(mbuf, height, width);
  oevent_list_clear(oevent_list);
  Oper_phase0_extras phase0_extras;
  phase0_extras.bank = bank;
  phase0_extras.bank_size = 0;
  phase0_extras.vars_slots = &vars_slots[0];
  sim_phase_0(gbuf, mbuf, height, width, tick_number, &phase0_extras);
  Oper_phase1_extras phase1_extras;
  phase1_extras.bank = bank;
  phase1_extras.bank_size = phase0_extras.bank_size;
  bank_cursor_reset(&phase1_extras.cursor);
  phase1_extras.vars_slots = &vars_slots[0];
  phase1_extras.piano_bits = piano_bits;
  phase1_extras.oevent_list = oevent_list;
  sim_phase_1(gbuf, mbuf, height, width, tick_number, &phase1_extras);
}
