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
static inline Usz semantic_index_of_glyph(Glyph c) {
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

static inline bool oper_has_neighboring_bang(Gbuffer gbuf, Usz h, Usz w, Usz y,
                                             Usz x) {
  return gbuffer_peek_relative(gbuf, h, w, y, x, 0, 1) == '*' ||
         gbuffer_peek_relative(gbuf, h, w, y, x, 0, -1) == '*' ||
         gbuffer_peek_relative(gbuf, h, w, y, x, 1, 0) == '*' ||
         gbuffer_peek_relative(gbuf, h, w, y, x, -1, 0) == '*';
}

static inline void oper_move_relative_or_explode(Gbuffer gbuf, Mbuffer mbuf,
                                                 Usz height, Usz width,
                                                 Glyph moved, Usz y, Usz x,
                                                 Isz delta_y, Isz delta_x) {
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
  Bank_cursor read_cursor;
} Oper_bank_params;

// static may cause warning if programmer doesn't use bank storage
void oper_bank_store(Oper_bank_params* bank_params, Usz width, Usz y, Usz x,
                     Glyph* restrict glyphs, Usz num_glyphs) {
  assert(num_glyphs > 0);
  Usz index = y * width + x;
  assert(index < ORCA_BANK_INDEX_MAX);
  bank_params->size = bank_append(bank_params->bank, bank_params->size, index,
                                  glyphs, num_glyphs);
}
Usz oper_bank_load(Oper_bank_params* bank_params, Usz width, Usz y, Usz x,
                   Glyph* restrict out_glyphs, Usz out_count) {
  Usz index = y * width + x;
  assert(index < ORCA_BANK_INDEX_MAX);
  return bank_read(bank_params->bank->data, bank_params->size,
                   &bank_params->read_cursor, index, out_glyphs, out_count);
}

ORCA_FORCE_STATIC_INLINE
Usz UCLAMP(Usz val, Usz min, Usz max) {
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
#define ORCA_DEFINE_OPER_CHARS(_solo_defs, _dual_defs)                         \
  enum Orca_oper_chars {                                                       \
    _solo_defs(ORCA_EXPAND_SOLO_OPER_CHARS)                                    \
        _dual_defs(ORCA_EXPAND_DUAL_OPER_CHARS)                                \
  };
#define ORCA_DECLARE_OPERATORS(_solo_defs, _dual_defs)                         \
  ORCA_DEFINE_OPER_CHARS(_solo_defs, _dual_defs)

#define OPER_IGNORE_COMMON_ARGS()                                              \
  (void)gbuffer;                                                               \
  (void)mbuffer;                                                               \
  (void)height;                                                                \
  (void)width;                                                                 \
  (void)y;                                                                     \
  (void)x;

#define BEGIN_SOLO_PHASE_0(_oper_name)                                         \
  static inline void oper_phase0_##_oper_name(                                 \
      Gbuffer const gbuffer, Mbuffer const mbuffer, Usz const height,          \
      Usz const width, Usz const y, Usz const x, U8 const cell_flags) {        \
    OPER_IGNORE_COMMON_ARGS()                                                  \
    (void)cell_flags;                                                          \
    enum { This_oper_char = Orca_oper_char_##_oper_name };
#define BEGIN_SOLO_PHASE_1(_oper_name)                                         \
  static inline void oper_phase1_##_oper_name(                                 \
      Gbuffer const gbuffer, Mbuffer const mbuffer, Usz const height,          \
      Usz const width, Usz const y, Usz const x) {                             \
    OPER_IGNORE_COMMON_ARGS()                                                  \
    enum { This_oper_char = Orca_oper_char_##_oper_name };
#define BEGIN_DUAL_PHASE_0(_oper_name)                                         \
  static inline void oper_phase0_##_oper_name(                                 \
      Gbuffer const gbuffer, Mbuffer const mbuffer, Usz const height,          \
      Usz const width, Usz const y, Usz const x, U8 const cell_flags,          \
      Glyph const This_oper_char) {                                            \
    OPER_IGNORE_COMMON_ARGS()                                                  \
    (void)cell_flags;                                                          \
    bool const Dual_is_uppercase =                                             \
        Orca_oper_upper_char_##_oper_name == This_oper_char;                   \
    (void)Dual_is_uppercase;
#define BEGIN_DUAL_PHASE_1(_oper_name)                                         \
  static inline void oper_phase1_##_oper_name(                                 \
      Gbuffer const gbuffer, Mbuffer const mbuffer, Usz const height,          \
      Usz const width, Usz const y, Usz const x, Glyph const This_oper_char) { \
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

#define LOCKING Mark_flag_lock
#define NONLOCKING Mark_flag_none
#define HASTE Mark_flag_haste_input

#define REALIZE_DUAL                                                           \
  bool const Dual_is_active =                                                  \
      Dual_is_uppercase |                                                      \
      oper_has_neighboring_bang(gbuffer, height, width, y, x);

#define BEGIN_DUAL_PORTS                                                       \
  {                                                                            \
    bool const Oper_ports_enabled = Dual_is_active;

#define STOP_IF_DUAL_INACTIVE                                                  \
  if (!Dual_is_active)                                                         \
  return

#define STOP_IF_NOT_BANGED                                                     \
  if (!oper_has_neighboring_bang(gbuffer, height, width, y, x))                \
  return

#define I_PORT(_delta_y, _delta_x, _flags)                                     \
  mbuffer_poke_relative_flags_or(                                              \
      mbuffer, height, width, y, x, _delta_y, _delta_x,                        \
      Mark_flag_input | ((_flags)&Mark_flag_haste_input) |                     \
          (Oper_ports_enabled &&                                               \
                   !(cell_flags & (Mark_flag_lock | Mark_flag_sleep))          \
               ? (_flags)                                                      \
               : Mark_flag_none))
#define O_PORT(_delta_y, _delta_x, _flags)                                     \
  mbuffer_poke_relative_flags_or(                                              \
      mbuffer, height, width, y, x, _delta_y, _delta_x,                        \
      Mark_flag_input | ((_flags)&Mark_flag_haste_input) |                     \
          (Oper_ports_enabled &&                                               \
                   !(cell_flags & (Mark_flag_lock | Mark_flag_sleep))          \
               ? (_flags)                                                      \
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

#define ORCA_SOLO_OPERATORS(_) _('*', bang)

#define ORCA_DUAL_OPERATORS(_)                                                 \
  _('N', 'n', north)                                                           \
  _('E', 'e', east)                                                            \
  _('S', 's', south)                                                           \
  _('W', 'w', west)                                                            \
  _('Z', 'z', southeast)                                                       \
  _('A', 'a', add)                                                             \
  _('G', 'g', generator)                                                       \
  _('H', 'h', halt)                                                            \
  _('I', 'i', increment)                                                       \
  _('J', 'j', jump)                                                            \
  _('M', 'm', modulo)                                                          \
  _('O', 'o', offset)

ORCA_DECLARE_OPERATORS(ORCA_SOLO_OPERATORS, ORCA_DUAL_OPERATORS)

MOVING_OPERATOR(north, -1, 0)
MOVING_OPERATOR(east, 0, 1)
MOVING_OPERATOR(south, 1, 0)
MOVING_OPERATOR(west, 0, -1)
MOVING_OPERATOR(southeast, 1, 1)

BEGIN_SOLO_PHASE_0(bang)
  BEGIN_HASTE
    BECOME('.');
  END_HASTE
END_PHASE
BEGIN_SOLO_PHASE_1(bang)
END_PHASE

BEGIN_DUAL_PHASE_0(add)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    I_PORT(0, 1, LOCKING);
    I_PORT(0, 2, LOCKING);
    O_PORT(1, 0, LOCKING);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(add)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  POKE(1, 0, glyphs_add(PEEK(0, 1), PEEK(0, 2)));
END_PHASE

BEGIN_DUAL_PHASE_0(generator)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    I_PORT(0, 1, LOCKING);
    O_PORT(1, 0, NONLOCKING);
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
    O_PORT(1, 0, LOCKING);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(halt)
END_PHASE

BEGIN_DUAL_PHASE_0(increment)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    I_PORT(0, 1, LOCKING);
    I_PORT(0, 2, LOCKING);
    O_PORT(1, 0, LOCKING);
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
    I_PORT(-1, 0, LOCKING);
    O_PORT(1, 0, LOCKING);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(jump)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  POKE(1, 0, PEEK(-1, 0));
END_PHASE

BEGIN_DUAL_PHASE_0(modulo)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    I_PORT(0, 1, LOCKING);
    I_PORT(0, 2, LOCKING);
    O_PORT(1, 0, LOCKING);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(modulo)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  POKE(1, 0, glyphs_mod(PEEK(0, 1), PEEK(0, 2)));
END_PHASE

BEGIN_DUAL_PHASE_0(offset)
  REALIZE_DUAL;
  Isz read_y = (Isz)UCLAMP(INDEX(PEEK(0, -1)), 0, 16);
  Isz read_x = (Isz)UCLAMP(INDEX(PEEK(0, -2)), 1, 16);
  BEGIN_DUAL_PORTS
    I_PORT(0, -1, LOCKING | HASTE);
    I_PORT(0, -2, LOCKING | HASTE);
    I_PORT(read_y, read_x, LOCKING);
    O_PORT(0, 1, LOCKING);
  END_PORTS
  // wrong
  STOP_IF_DUAL_INACTIVE;
  BEGIN_HASTE
    POKE(0, 1, PEEK(read_y, read_x));
    STUN(0, 1);
  END_HASTE
END_PHASE
BEGIN_DUAL_PHASE_1(offset)
END_PHASE

//////// Run simulation

#define SIM_EXPAND_SOLO_PHASE_0(_oper_char, _oper_name)                        \
  case _oper_char:                                                             \
    oper_phase0_##_oper_name(gbuf, mbuf, height, width, iy, ix, cell_flags);   \
    break;
#define SIM_EXPAND_SOLO_PHASE_1(_oper_char, _oper_name)                        \
  case _oper_char:                                                             \
    oper_phase1_##_oper_name(gbuf, mbuf, height, width, iy, ix);               \
    break;

#define SIM_EXPAND_DUAL_PHASE_0(_upper_oper_char, _lower_oper_char,            \
                                _oper_name)                                    \
  case _upper_oper_char:                                                       \
  case _lower_oper_char:                                                       \
    oper_phase0_##_oper_name(gbuf, mbuf, height, width, iy, ix, cell_flags,    \
                             glyph_char);                                      \
    break;
#define SIM_EXPAND_DUAL_PHASE_1(_upper_oper_char, _lower_oper_char,            \
                                _oper_name)                                    \
  case _upper_oper_char:                                                       \
  case _lower_oper_char:                                                       \
    oper_phase1_##_oper_name(gbuf, mbuf, height, width, iy, ix, glyph_char);   \
    break;

static void sim_phase_0(Gbuffer gbuf, Mbuffer mbuf, Usz height, Usz width,
                        Bank* bank) {
  (void)bank;
  for (Usz iy = 0; iy < height; ++iy) {
    Glyph* glyph_row = gbuf + iy * width;
    for (Usz ix = 0; ix < width; ++ix) {
      Glyph glyph_char = glyph_row[ix];
      if (glyph_char == '.')
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
                        Bank* bank) {
  (void)bank;
  for (Usz iy = 0; iy < height; ++iy) {
    Glyph* glyph_row = gbuf + iy * width;
    for (Usz ix = 0; ix < width; ++ix) {
      Glyph glyph_char = glyph_row[ix];
      if (glyph_char == '.')
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

void orca_run(Gbuffer gbuf, Mbuffer mbuf, Usz height, Usz width, Bank* bank) {
  mbuffer_clear(mbuf, height, width);
  sim_phase_0(gbuf, mbuf, height, width, bank);
  sim_phase_1(gbuf, mbuf, height, width, bank);
}
