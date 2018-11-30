#include "gbuffer.h"
#include "mark.h"
#include "sim.h"

//////// Utilities

static Glyph const indexed_glyphs[] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd',
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r',
    's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '.', '*', ':', ';', '#',
};

enum { Glyphs_array_num = sizeof indexed_glyphs };

static inline Glyph glyph_lowered(Glyph c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c - ('a' - 'A')) : c;
}

// Always returns 0 through (sizeof indexed_glyphs) - 1, and works on
// capitalized glyphs as well. The index of the lower-cased glyph is returned
// if the glyph is capitalized.
static ORCA_FORCE_NO_INLINE Usz semantic_index_of_glyph(Glyph c) {
  Glyph c0 = glyph_lowered(c);
  if (c0 == '.')
    return 0;
  for (Usz i = 0; i < Glyphs_array_num; ++i) {
    if (indexed_glyphs[i] == c0)
      return i;
  }
  return 0;
}

static inline Glyph glyphs_add(Glyph a, Glyph b) {
  Usz ia = semantic_index_of_glyph(a);
  Usz ib = semantic_index_of_glyph(b);
  return indexed_glyphs[(ia + ib) % Glyphs_array_num];
}

static inline Glyph glyphs_mod(Glyph a, Glyph b) {
  Usz ia = semantic_index_of_glyph(a);
  Usz ib = semantic_index_of_glyph(b);
  return indexed_glyphs[ib == 0 ? 0 : (ia % ib)];
}

// todo check if these inlines are actually being inlinded -- might be bad,
// should probably mark them not inlined

static bool oper_has_neighboring_bang(Gbuffer gbuf, Usz h, Usz w, Usz y,
                                      Usz x) {
  return gbuffer_peek_relative(gbuf, h, w, y, x, 0, 1) == '*' ||
         gbuffer_peek_relative(gbuf, h, w, y, x, 0, -1) == '*' ||
         gbuffer_peek_relative(gbuf, h, w, y, x, 1, 0) == '*' ||
         gbuffer_peek_relative(gbuf, h, w, y, x, -1, 0) == '*';
}

static ORCA_FORCE_NO_INLINE void
oper_move_relative_or_explode(Gbuffer gbuf, Mbuffer mbuf, Usz height, Usz width,
                              Glyph moved, Usz y, Usz x, Isz delta_y,
                              Isz delta_x) {
  Isz y0 = (Isz)y + delta_y;
  Isz x0 = (Isz)x + delta_x;
  if (y0 >= (Isz)height || x0 >= (Isz)width || y0 < 0 || x0 < 0) {
    gbuf[y * width + x] = '*';
    return;
  }
  Glyph* at_dest = gbuf + (Usz)y0 * width + (Usz)x0;
  if (*at_dest != '.') {
    gbuf[y * width + x] = '*';
    mbuffer_poke_flags_or(mbuf, height, width, y, x, Mark_flag_sleep);
    return;
  }
  *at_dest = moved;
  mbuffer_poke_flags_or(mbuf, height, width, (Usz)y0, (Usz)x0, Mark_flag_sleep);
  gbuf[y * width + x] = '.';
}

typedef struct {
  Bank* bank;
  Usz size;
} Oper_bank_write_params;

typedef struct {
  Bank* bank;
  Usz size;
  Bank_cursor cursor;
} Oper_bank_read_params;

// static may cause warning if programmer doesn't use bank storage
void ORCA_FORCE_NO_INLINE oper_bank_store(Oper_bank_write_params* bank_params,
                                          Usz width, Usz y, Usz x,
                                          I32* restrict vals, Usz num_vals) {
  assert(num_vals > 0);
  Usz index = y * width + x;
  assert(index < ORCA_BANK_INDEX_MAX);
  bank_params->size =
      bank_append(bank_params->bank, bank_params->size, index, vals, num_vals);
}
Usz ORCA_FORCE_NO_INLINE oper_bank_load(Oper_bank_read_params* bank_params,
                                        Usz width, Usz y, Usz x,
                                        I32* restrict out_vals, Usz out_count) {
  Usz index = y * width + x;
  assert(index < ORCA_BANK_INDEX_MAX);
  return bank_read(bank_params->bank->data, bank_params->size,
                   &bank_params->cursor, index, out_vals, out_count);
}

ORCA_FORCE_STATIC_INLINE
Usz usz_clamp(Usz val, Usz min, Usz max) {
  if (val < min)
    return min;
  if (val > max)
    return max;
  return val;
}

#if defined(__GNUC__) || defined(__clang__)
#define ORCA_ASSERT_IS_ARRAY(_array)                                           \
  (sizeof(char[1 - 2 * __builtin_types_compatible_p(                           \
                           __typeof(_array), __typeof(&(_array)[0]))]) -       \
   1)
#define ORCA_ARRAY_COUNTOF(_array)                                             \
  (sizeof(_array) / sizeof((_array)[0]) + ORCA_ASSERT_IS_ARRAY(_array))
#else
// pray
#define ORCA_ARRAY_COUNTOF(_array) (sizeof(_array) / sizeof(_array[0]))
#endif

#define ORCA_EXPAND_SOLO_OPER_CHARS(_oper_char, _oper_name)                    \
  Orca_oper_char_##_oper_name = _oper_char,
#define ORCA_EXPAND_DUAL_OPER_CHARS(_upper_oper_char, _lower_oper_char,        \
                                    _oper_name)                                \
  Orca_oper_upper_char_##_oper_name = _upper_oper_char,                        \
  Orca_oper_lower_char_##_oper_name = _lower_oper_char,
#define ORCA_DEFINE_OPER_CHARS(_solo_defs, _dual_defs)                         \
  enum Orca_oper_chars {                                                       \
    _solo_defs(ORCA_EXPAND_SOLO_OPER_CHARS)                                    \
        _dual_defs(ORCA_EXPAND_DUAL_OPER_CHARS)                                \
  };
#define ORCA_DECLARE_OPERATORS(_solo_defs, _dual_defs)                         \
  ORCA_DEFINE_OPER_CHARS(_solo_defs, _dual_defs)

#define OPER_PHASE_COMMON_ARGS                                                 \
  Gbuffer const gbuffer, Mbuffer const mbuffer, Usz const height,              \
      Usz const width, Usz const y, Usz const x, Usz Tick_number
#define OPER_PHASE_0_COMMON_ARGS                                               \
  OPER_PHASE_COMMON_ARGS, Oper_bank_write_params *const bank_params,           \
      U8 const cell_flags
#define OPER_PHASE_1_COMMON_ARGS                                               \
  OPER_PHASE_COMMON_ARGS, Oper_bank_read_params* const bank_params

#define OPER_IGNORE_COMMON_ARGS()                                              \
  (void)gbuffer;                                                               \
  (void)mbuffer;                                                               \
  (void)height;                                                                \
  (void)width;                                                                 \
  (void)y;                                                                     \
  (void)x;                                                                     \
  (void)Tick_number;                                                           \
  (void)bank_params;

#define OPER_PHASE_SPEC static ORCA_FORCE_NO_INLINE

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
    bool const Dual_is_uppercase =                                             \
        Orca_oper_upper_char_##_oper_name == This_oper_char;                   \
    (void)Dual_is_uppercase;
#define BEGIN_DUAL_PHASE_1(_oper_name)                                         \
  OPER_PHASE_SPEC void oper_phase1_##_oper_name(OPER_PHASE_1_COMMON_ARGS,      \
                                                Glyph const This_oper_char) {  \
    OPER_IGNORE_COMMON_ARGS()                                                  \
    bool const Dual_is_uppercase =                                             \
        Orca_oper_upper_char_##_oper_name == This_oper_char;                   \
    (void)Dual_is_uppercase;

#define END_PHASE }

#define INDEX(_glyph) semantic_index_of_glyph(_glyph)
#define GLYPH(_index) indexed_glyphs[_index]
#define PEEK(_delta_y, _delta_x)                                               \
  gbuffer_peek_relative(gbuffer, height, width, y, x, _delta_y, _delta_x)
#define POKE(_delta_y, _delta_x, _glyph)                                       \
  gbuffer_poke_relative(gbuffer, height, width, y, x, _delta_y, _delta_x,      \
                        _glyph)
#define BECOME(_glyph) gbuffer_poke(gbuffer, height, width, y, x, _glyph)
#define STUN(_delta_y, _delta_x)                                               \
  mbuffer_poke_relative_flags_or(mbuffer, height, width, y, x, _delta_y,       \
                                 _delta_x, Mark_flag_sleep)
#define LOCK(_delta_y, _delta_x)                                               \
  mbuffer_poke_relative_flags_or(mbuffer, height, width, y, x, _delta_y,       \
                                 _delta_x, Mark_flag_lock)

#define STORE(_i32_array)                                                      \
  oper_bank_store(bank_params, width, y, x, _i32_array,                        \
                  ORCA_ARRAY_COUNTOF(_i32_array))
#define LOAD(_i32_array)                                                       \
  oper_bank_load(bank_params, width, y, x, _i32_array,                         \
                 ORCA_ARRAY_COUNTOF(_i32_array))

#define IN Mark_flag_input
#define OUT Mark_flag_output
#define NONLOCKING Mark_flag_lock
#define HASTE Mark_flag_haste_input

#define REALIZE_DUAL                                                           \
  bool const Dual_is_active =                                                  \
      Dual_is_uppercase |                                                      \
      oper_has_neighboring_bang(gbuffer, height, width, y, x);

#define PSEUDO_DUAL bool const Dual_is_active = true

#define BEGIN_DUAL_PORTS                                                       \
  {                                                                            \
    bool const Oper_ports_enabled = Dual_is_active;

#define DUAL_IS_ACTIVE Dual_is_active

#define IS_AWAKE (!(cell_flags & (Mark_flag_lock | Mark_flag_sleep)))

#define STOP_IF_DUAL_INACTIVE                                                  \
  if (!Dual_is_active)                                                         \
  return

#define STOP_IF_NOT_BANGED                                                     \
  if (!oper_has_neighboring_bang(gbuffer, height, width, y, x))                \
  return

#define END_IF }

#define OPER_PORT_IO_MASK                                                      \
  (Mark_flag_input | Mark_flag_output | Mark_flag_haste_input)
#define OPER_PORT_CELL_ENABLING_MASK (Mark_flag_lock | Mark_flag_sleep)
#define OPER_PORT_FLIP_LOCK_BIT(_flags) ((_flags) ^ Mark_flag_lock)

#define PORT(_delta_y, _delta_x, _flags)                                       \
  mbuffer_poke_relative_flags_or(                                              \
      mbuffer, height, width, y, x, _delta_y, _delta_x,                        \
      ((_flags)&OPER_PORT_IO_MASK) |                                           \
          (Oper_ports_enabled && !(cell_flags & OPER_PORT_CELL_ENABLING_MASK)  \
               ? OPER_PORT_FLIP_LOCK_BIT(_flags)                               \
               : Mark_flag_none))
#define END_PORTS }

#define BEGIN_HASTE if (!(cell_flags & (Mark_flag_lock | Mark_flag_sleep))) {
#define END_HASTE }

#define OPER_MOVE_OR_EXPLODE(_delta_y, _delta_x)                               \
  oper_move_relative_or_explode(gbuffer, mbuffer, height, width,               \
                                This_oper_char, y, x, _delta_y, _delta_x)

#define MOVING_OPERATOR(_oper_name, _delta_y, _delta_x)                        \
  BEGIN_DUAL_PHASE_0(_oper_name)                                               \
    BEGIN_HASTE                                                                \
      REALIZE_DUAL;                                                            \
      STOP_IF_DUAL_INACTIVE;                                                   \
      OPER_MOVE_OR_EXPLODE(_delta_y, _delta_x);                                \
    END_HASTE                                                                  \
  END_PHASE                                                                    \
  BEGIN_DUAL_PHASE_1(_oper_name)                                               \
  END_PHASE

//////// Operators

#define ORCA_SOLO_OPERATORS(_)                                                 \
  _('#', comment)                                                              \
  _('*', bang)

#define ORCA_DUAL_OPERATORS(_)                                                 \
  _('N', 'n', north)                                                           \
  _('E', 'e', east)                                                            \
  _('S', 's', south)                                                           \
  _('W', 'w', west)                                                            \
  _('Z', 'z', southeast)                                                       \
  _('A', 'a', add)                                                             \
  _('B', 'b', banger)                                                          \
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
  _('Q', 'q', count)                                                           \
  _('R', 'r', random)                                                          \
  _('T', 't', track)                                                           \
  _('U', 'u', uturn)                                                           \
  _('V', 'v', beam)                                                            \
  _('X', 'x', teleport)

ORCA_DECLARE_OPERATORS(ORCA_SOLO_OPERATORS, ORCA_DUAL_OPERATORS)

MOVING_OPERATOR(north, -1, 0)
MOVING_OPERATOR(east, 0, 1)
MOVING_OPERATOR(south, 1, 0)
MOVING_OPERATOR(west, 0, -1)
MOVING_OPERATOR(southeast, 1, 1)

#define MOVEMENT_CASES                                                         \
  'N' : case 'n' : case 'E' : case 'e' : case 'S' : case 's' : case 'W'        \
      : case 'w' : case 'Z' : case 'z'

BEGIN_SOLO_PHASE_0(bang)
  BEGIN_HASTE
    BECOME('.');
  END_HASTE
END_PHASE
BEGIN_SOLO_PHASE_1(bang)
END_PHASE

BEGIN_SOLO_PHASE_0(comment)
  if (!IS_AWAKE)
    return;
  Glyph* line = gbuffer + y * width;
  Usz max_x = x + 255;
  if (width < max_x)
    max_x = width;
  for (Usz x0 = x + 1; x0 < max_x; ++x0) {
    Glyph g = line[x0];
    mbuffer_poke_flags_or(mbuffer, height, width, y, x0, Mark_flag_lock);
    if (g == '#')
      break;
  }
END_PHASE
BEGIN_SOLO_PHASE_1(comment)
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

BEGIN_DUAL_PHASE_0(delay)
  PSEUDO_DUAL;
  bool out_is_nonlocking = false;
  if (DUAL_IS_ACTIVE) {
    BEGIN_HASTE
      out_is_nonlocking = INDEX(PEEK(0, -2)) == 0;
    END_HASTE
  }
  BEGIN_DUAL_PORTS
    PORT(0, -2, IN | HASTE);
    PORT(0, -1, IN | HASTE);
    PORT(1, 0, OUT | (out_is_nonlocking ? NONLOCKING : 0));
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(delay)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  Usz tick = INDEX(PEEK(0, -2));
  Glyph timer = PEEK(0, -1);
  POKE(0, -2, tick == 0 ? timer : GLYPH(tick - 1));
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
  POKE(1, 0, g0 == g1 ? '1' : '0');
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
  POKE(1, 0, PEEK(0, 1));
  STUN(0, 1);
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
  Usz min = INDEX(PEEK(0, 1));
  Usz max = INDEX(PEEK(0, 2));
  Usz val = INDEX(PEEK(1, 0));
  ++val;
  if (max == 0)
    max = 10;
  if (val >= max)
    val = min;
  POKE(1, 0, GLYPH(val));
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
  BEGIN_HASTE
    POKE(1, 0, '.');
  END_HASTE
END_PHASE
BEGIN_DUAL_PHASE_1(kill)
END_PHASE

BEGIN_DUAL_PHASE_0(loop)
  PSEUDO_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, -1, IN | HASTE);
  END_PORTS
  if (IS_AWAKE) {
    Usz len = usz_clamp(INDEX(PEEK(0, -1)), 1, 16);
    I32 len_data[1];
    len_data[0] = (I32)len;
    STORE(len_data);
    // todo optimize
    for (Usz i = 0; i < len; ++i) {
      LOCK(0, (Isz)i + 1);
    }
  }
END_PHASE
BEGIN_DUAL_PHASE_1(loop)
  I32 len_data[1];
  // todo should at least stun the 1 column if columns is 1
  if (LOAD(len_data) && len_data[0] >= 1 && len_data[0] <= 16) {
    Usz len = (Usz)len_data[0];
    Glyph buff[15];
    Glyph* gs = gbuffer + y * width + x + 1;
    Glyph hopped = *gs;
    // ORCA_MEMCPY(buff, gs + 1, len - 1);
    for (Usz i = 0; i < len - 1; ++i) {
      buff[i] = gs[i + 1];
    }
    buff[len - 1] = hopped;
    // ORCA_MEMCPY(gs, buff, len);
    for (Usz i = 0; i < len; ++i) {
      gs[i] = buff[i];
    }
    for (Usz i = 0; i < len; ++i) {
      STUN(0, (Isz)i + 1);
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
  POKE(1, 0, glyphs_mod(PEEK(0, 1), PEEK(0, 2)));
END_PHASE

BEGIN_DUAL_PHASE_0(offset)
  REALIZE_DUAL;
  I32 coords[2];
  coords[0] = 0; // y
  coords[1] = 1; // x
  if (DUAL_IS_ACTIVE) {
    coords[0] = (I32)usz_clamp(INDEX(PEEK(0, -1)), 0, 16);
    coords[1] = (I32)usz_clamp(INDEX(PEEK(0, -2)) + 1, 1, 16);
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
  STUN(1, 0);
END_PHASE

BEGIN_DUAL_PHASE_0(push)
  REALIZE_DUAL;
  I32 write_val_x[1];
  write_val_x[0] = 0;
  if (DUAL_IS_ACTIVE && IS_AWAKE) {
    Usz len = usz_clamp(INDEX(PEEK(0, -1)), 1, 16);
    Usz key = INDEX(PEEK(0, -2));
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

BEGIN_DUAL_PHASE_0(count)
  PSEUDO_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, -1, IN | HASTE);
    PORT(1, 0, OUT);
  END_PORTS
  if (IS_AWAKE) {
    Usz len = usz_clamp(INDEX(PEEK(0, -1)), 0, 16) + 1;
    I32 len_data[1];
    len_data[0] = (I32)len;
    STORE(len_data);
    Usz max_x = x + len;
    if (max_x > width)
      max_x = width;
    U8* i = mbuffer + y * width + x + 1;
    U8* e = mbuffer + y * width + max_x;
    while (i != e) {
      *i = (U8)(*i | Mark_flag_lock);
      ++i;
    }
  }
END_PHASE
BEGIN_DUAL_PHASE_1(count)
  I32 len_data[1];
  if (LOAD(len_data) && len_data[0] >= 1 && len_data[0] <= 17) {
    Usz len = (Usz)len_data[0];
    Usz max_x = x + len;
    if (max_x >= width)
      max_x = width;
    Glyph const* i = gbuffer + y * width + x + 1;
    Glyph const* e = gbuffer + y * width + max_x;
    Usz count = 0;
    while (i != e) {
      if (*i != '.')
        ++count;
      ++i;
    }
    Glyph g = GLYPH(count % Glyphs_array_num);
    POKE(1, 0, g);
  }
END_PHASE

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
  Usz a = INDEX(PEEK(0, 1));
  Usz b = INDEX(PEEK(0, 2));
  Usz min, max;
  if (a == b) {
    POKE(1, 0, GLYPH(a));
    return;
  } else if (a < b) {
    min = a;
    max = b;
  } else {
    min = b;
    max = a;
  }
  Usz val = (y * 5 + x * 3) * Tick_number % (max - min) + min;
  POKE(1, 0, GLYPH(val));
END_PHASE

BEGIN_DUAL_PHASE_0(track)
  PSEUDO_DUAL;
  Isz read_val_x = 1;
  if (IS_AWAKE) {
    Usz len = usz_clamp(INDEX(PEEK(0, -1)), 1, 16);
    Usz key = INDEX(PEEK(0, -2));
    read_val_x = (Isz)(key % len + 1);
    I32 ival[1];
    ival[0] = (I32)read_val_x;
    STORE(ival);
    for (Isz i = 0; i < read_val_x; ++i) {
      LOCK(0, i + 1);
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
  STUN(1, 0);
END_PHASE

#define UTURN_DIRS(_)                                                          \
  _(-1, 0, 'N')                                                                \
  _(0, -1, 'W')                                                                \
  _(0, 1, 'E')                                                                 \
  _(1, 0, 'S')

BEGIN_DUAL_PHASE_0(uturn)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
#define X(_d_y, _d_x, _d_glyph) PORT(_d_y, _d_x, IN | OUT | HASTE | NONLOCKING);
    UTURN_DIRS(X)
#undef X
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(uturn)
#define X(_d_y, _d_x, _d_glyph)                                                \
  {                                                                            \
    Glyph g = PEEK(_d_y, _d_x);                                                \
    switch (g) {                                                               \
    case MOVEMENT_CASES:                                                       \
      POKE(_d_y, _d_x, _d_glyph);                                              \
      STUN(_d_y, _d_x);                                                        \
    }                                                                          \
  }
  UTURN_DIRS(X)
#undef X
END_PHASE

#undef UTURN_DIRS

BEGIN_DUAL_PHASE_0(beam)
  if (!IS_AWAKE)
    return;
  Usz max_y = y + 255;
  if (height < max_y)
    max_y = height;
  Glyph* col = gbuffer + x;
  Usz y0 = y;
  for (;;) {
    if (y0 + 1 == max_y)
      break;
    Glyph g = col[width * (y0 + 1)];
    if (g == '.' || g == '*')
      break;
    ++y0;
  }
  I32 val_y[1];
  val_y[0] = (I32)(y - y0);
  STORE(val_y);
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(val_y[0], 0, OUT | NONLOCKING);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(beam)
  STOP_IF_NOT_BANGED;
  I32 val_y[1];
  if (!LOAD(val_y))
    val_y[0] = 1;
  POKE(val_y[0], 0, '.');
END_PHASE

BEGIN_DUAL_PHASE_0(teleport)
  REALIZE_DUAL;
  I32 coords[2];
  coords[0] = 0; // y
  coords[1] = 1; // x
  if (DUAL_IS_ACTIVE) {
    coords[0] = (I32)usz_clamp(INDEX(PEEK(0, -1)), 0, 16);
    coords[1] = (I32)usz_clamp(INDEX(PEEK(0, -2)), 1, 16);
    STORE(coords);
  }
  BEGIN_DUAL_PORTS
    PORT(0, -1, IN | HASTE);
    PORT(0, -2, IN | HASTE);
    PORT(1, 0, IN);
    PORT(coords[0], coords[1], OUT | NONLOCKING);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(teleport)
  STOP_IF_NOT_BANGED;
  I32 coords[2];
  if (!LOAD(coords)) {
    coords[0] = 0;
    coords[1] = 1;
  }
  POKE(coords[0], coords[1], PEEK(0, 1));
  STUN(coords[0], coords[1]);
END_PHASE

//////// Run simulation

#define SIM_EXPAND_SOLO_PHASE_0(_oper_char, _oper_name)                        \
  case _oper_char:                                                             \
    oper_phase0_##_oper_name(gbuf, mbuf, height, width, iy, ix, tick_number,   \
                             bank_params, cell_flags);                         \
    break;
#define SIM_EXPAND_SOLO_PHASE_1(_oper_char, _oper_name)                        \
  case _oper_char:                                                             \
    oper_phase1_##_oper_name(gbuf, mbuf, height, width, iy, ix, tick_number,   \
                             bank_params);                                     \
    break;

#define SIM_EXPAND_DUAL_PHASE_0(_upper_oper_char, _lower_oper_char,            \
                                _oper_name)                                    \
  case _upper_oper_char:                                                       \
  case _lower_oper_char:                                                       \
    oper_phase0_##_oper_name(gbuf, mbuf, height, width, iy, ix, tick_number,   \
                             bank_params, cell_flags, glyph_char);             \
    break;
#define SIM_EXPAND_DUAL_PHASE_1(_upper_oper_char, _lower_oper_char,            \
                                _oper_name)                                    \
  case _upper_oper_char:                                                       \
  case _lower_oper_char:                                                       \
    oper_phase1_##_oper_name(gbuf, mbuf, height, width, iy, ix, tick_number,   \
                             bank_params, glyph_char);                         \
    break;

static void sim_phase_0(Gbuffer gbuf, Mbuffer mbuf, Usz height, Usz width,
                        Usz tick_number, Oper_bank_write_params* bank_params) {
  for (Usz iy = 0; iy < height; ++iy) {
    Glyph* glyph_row = gbuf + iy * width;
    for (Usz ix = 0; ix < width; ++ix) {
      Glyph glyph_char = glyph_row[ix];
      if (ORCA_LIKELY(glyph_char == '.'))
        continue;
      U8 cell_flags = mbuffer_peek(mbuf, height, width, iy, ix) &
                      (Mark_flag_lock | Mark_flag_sleep);
      switch (glyph_char) {
        ORCA_SOLO_OPERATORS(SIM_EXPAND_SOLO_PHASE_0)
        ORCA_DUAL_OPERATORS(SIM_EXPAND_DUAL_PHASE_0)
      }
    }
  }
}

static void sim_phase_1(Gbuffer gbuf, Mbuffer mbuf, Usz height, Usz width,
                        Usz tick_number, Oper_bank_read_params* bank_params) {
  for (Usz iy = 0; iy < height; ++iy) {
    Glyph* glyph_row = gbuf + iy * width;
    for (Usz ix = 0; ix < width; ++ix) {
      Glyph glyph_char = glyph_row[ix];
      if (ORCA_LIKELY(glyph_char == '.'))
        continue;
      if (mbuffer_peek(mbuf, height, width, iy, ix) &
          (Mark_flag_lock | Mark_flag_sleep))
        continue;
      switch (glyph_char) {
        ORCA_SOLO_OPERATORS(SIM_EXPAND_SOLO_PHASE_1)
        ORCA_DUAL_OPERATORS(SIM_EXPAND_DUAL_PHASE_1)
      }
    }
  }
}

void orca_run(Gbuffer gbuf, Mbuffer mbuf, Usz height, Usz width,
              Usz tick_number, Bank* bank) {
  mbuffer_clear(mbuf, height, width);
  Oper_bank_write_params bank_write_params;
  bank_write_params.bank = bank;
  bank_write_params.size = 0;
  sim_phase_0(gbuf, mbuf, height, width, tick_number, &bank_write_params);
  Oper_bank_read_params bank_read_params;
  bank_read_params.bank = bank;
  bank_read_params.size = bank_write_params.size;
  bank_cursor_reset(&bank_read_params.cursor);
  sim_phase_1(gbuf, mbuf, height, width, tick_number, &bank_read_params);
}
