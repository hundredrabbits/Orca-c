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
  if (c0 == '.') return 0;
  for (Usz i = 0; i < Glyphs_array_num; ++i) {
    if (indexed_glyphs[i] == c0)
      return i;
  }
  return 0;
}

static inline Glyph glyphs_sum(Glyph a, Glyph b) {
  Usz ia = semantic_index_of_glyph(a);
  Usz ib = semantic_index_of_glyph(b);
  return indexed_glyphs[(ia + ib) % Glyphs_array_num];
}

static inline Glyph glyphs_mod(Glyph a, Glyph b) {
  Usz ia = semantic_index_of_glyph(a);
  Usz ib = semantic_index_of_glyph(b);
  return indexed_glyphs[ib == 0 ? 0 : (ia % ib)];
}

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

#define ORCA_EXPAND_OPER_CHARS(_oper_name, _oper_char)                         \
  Orca_oper_char_##_oper_name = _oper_char,
#define ORCA_DEFINE_OPER_CHARS(_defs)                                          \
  enum Orca_oper_chars { _defs(ORCA_EXPAND_OPER_CHARS) };
#define ORCA_DECLARE_OPERATORS(_defs) ORCA_DEFINE_OPER_CHARS(_defs)

#define OPER_PHASE_N(_phase_number, _oper_name)                                \
  static inline void oper_phase##_phase_number##_##_oper_name(                 \
      Gbuffer const gbuffer, Mbuffer const mbuffer, Usz const height,          \
      Usz const width, Usz const y, Usz const x) {                             \
    (void)gbuffer;                                                             \
    (void)mbuffer;                                                             \
    (void)height;                                                              \
    (void)width;                                                               \
    (void)y;                                                                   \
    (void)x;                                                                   \
    enum { This_oper_char = Orca_oper_char_##_oper_name };

#define OPER_PHASE_0(_oper_name) OPER_PHASE_N(0, _oper_name)
#define OPER_PHASE_1(_oper_name) OPER_PHASE_N(1, _oper_name)
#define OPER_PHASE_2(_oper_name) OPER_PHASE_N(2, _oper_name)
#define OPER_END }

#define OPER_POKE_ABSOLUTE(_y, _x, _glyph)                                     \
  gbuffer_poke(gbuffer, height, width, _y, _x, _glyph)
#define OPER_PEEK_RELATIVE(_delta_y, _delta_x)                                 \
  gbuffer_peek_relative(gbuffer, height, width, y, x, _delta_y, _delta_x)
#define OPER_POKE_RELATIVE(_delta_y, _delta_x, _glyph)                         \
  gbuffer_poke_relative(gbuffer, height, width, y, x, _delta_y, _delta_x,      \
                        _glyph)
#define OPER_POKE_SELF(_glyph) OPER_POKE_ABSOLUTE(y, x, _glyph)

#define OPER_REQUIRE_BANG()                                                    \
  if (!oper_has_neighboring_bang(gbuffer, height, width, y, x))                \
  return

#define OPER_LOCK_RELATIVE(_delta_y, _delta_x)                                 \
  mbuffer_poke_relative_flags_or(mbuffer, height, width, y, x, _delta_y,       \
                                 _delta_x, Mark_flag_lock)

#define PORT_LOCKED Mark_flag_lock
#define PORT_UNLOCKED Mark_flag_none
#define PORT_HASTE Mark_flag_haste_input

#define OPER_PORT_INPUT(_delta_y, _delta_x, _flags)                            \
  mbuffer_poke_relative_flags_or(mbuffer, height, width, y, x, _delta_y,       \
                                 _delta_x, Mark_flag_input | _flags)

#define OPER_PORT_OUTPUT(_delta_y, _delta_x, _flags)                           \
  mbuffer_poke_relative_flags_or(mbuffer, height, width, y, x, _delta_y,       \
                                 _delta_x, Mark_flag_output | _flags)

#define OPER_MOVE_OR_EXPLODE(_delta_y, _delta_x)                               \
  oper_move_relative_or_explode(gbuffer, mbuffer, height, width,               \
                                This_oper_char, y, x, _delta_y, _delta_x)

#define OPER_DEFINE_UPPERCASE_DIRECTIONAL(_oper_name, _delta_y, _delta_x)      \
  OPER_PHASE_0(_oper_name)                                                     \
  OPER_END                                                                     \
  OPER_PHASE_1(_oper_name)                                                     \
    OPER_MOVE_OR_EXPLODE(_delta_y, _delta_x);                                  \
  OPER_END                                                                     \
  OPER_PHASE_2(_oper_name)                                                     \
  OPER_END

#define OPER_DEFINE_LOWERCASE_DIRECTIONAL(_oper_name, _delta_y, _delta_x)      \
  OPER_PHASE_0(_oper_name)                                                     \
  OPER_END                                                                     \
  OPER_PHASE_1(_oper_name)                                                     \
    OPER_REQUIRE_BANG();                                                       \
    OPER_MOVE_OR_EXPLODE(_delta_y, _delta_x);                                  \
  OPER_END                                                                     \
  OPER_PHASE_2(_oper_name)                                                     \
  OPER_END

//////// Operators

#define ORCA_OPERATORS(_)                                                      \
  _(North, 'N')                                                                \
  _(East, 'E')                                                                 \
  _(South, 'S')                                                                \
  _(West, 'W')                                                                 \
  _(north, 'n')                                                                \
  _(east, 'e')                                                                 \
  _(south, 's')                                                                \
  _(west, 'w')                                                                 \
  _(Add, 'A')                                                                  \
  _(Modulo, 'M')                                                               \
  _(Increment, 'I')                                                            \
  _(bang, '*')

ORCA_DECLARE_OPERATORS(ORCA_OPERATORS)

//////// Behavior

OPER_DEFINE_UPPERCASE_DIRECTIONAL(North, -1, 0)
OPER_DEFINE_UPPERCASE_DIRECTIONAL(East, 0, 1)
OPER_DEFINE_UPPERCASE_DIRECTIONAL(South, 1, 0)
OPER_DEFINE_UPPERCASE_DIRECTIONAL(West, 0, -1)
OPER_DEFINE_LOWERCASE_DIRECTIONAL(north, -1, 0)
OPER_DEFINE_LOWERCASE_DIRECTIONAL(east, 0, 1)
OPER_DEFINE_LOWERCASE_DIRECTIONAL(south, 1, 0)
OPER_DEFINE_LOWERCASE_DIRECTIONAL(west, 0, -1)

OPER_PHASE_0(Add)
  OPER_PORT_INPUT(0, 1, PORT_LOCKED);
  OPER_PORT_INPUT(0, 2, PORT_LOCKED);
  OPER_PORT_OUTPUT(1, 0, PORT_LOCKED);
OPER_END
OPER_PHASE_1(Add)
OPER_END
OPER_PHASE_2(Add)
  Glyph inp0 = OPER_PEEK_RELATIVE(0, 1);
  Glyph inp1 = OPER_PEEK_RELATIVE(0, 2);
  Glyph g = glyphs_sum(inp0, inp1);
  OPER_POKE_RELATIVE(1, 0, g);
OPER_END

OPER_PHASE_0(Modulo)
  OPER_PORT_INPUT(0, 1, PORT_LOCKED);
  OPER_PORT_INPUT(0, 2, PORT_LOCKED);
  OPER_PORT_OUTPUT(1, 0, PORT_LOCKED);
OPER_END
OPER_PHASE_1(Modulo)
OPER_END
OPER_PHASE_2(Modulo)
  Glyph inp0 = OPER_PEEK_RELATIVE(0, 1);
  Glyph inp1 = OPER_PEEK_RELATIVE(0, 2);
  Glyph g = glyphs_mod(inp0, inp1);
  OPER_POKE_RELATIVE(1, 0, g);
OPER_END

OPER_PHASE_0(Increment)
  OPER_PORT_INPUT(0, 1, PORT_LOCKED);
  OPER_PORT_INPUT(0, 2, PORT_LOCKED);
  OPER_PORT_OUTPUT(1, 0, PORT_LOCKED);
OPER_END
OPER_PHASE_1(Increment)
OPER_END
OPER_PHASE_2(Increment)
OPER_END

OPER_PHASE_0(bang)
OPER_END
OPER_PHASE_1(bang)
  OPER_POKE_SELF('.');
OPER_END
OPER_PHASE_2(bang)
OPER_END

//////// Run simulation

#define SIM_EXPAND_PHASE_0(_oper_name, _oper_char)                             \
  case _oper_char:                                                             \
    oper_phase0_##_oper_name(gbuf, mbuf, height, width, iy, ix);               \
    break;
#define SIM_EXPAND_PHASE_1(_oper_name, _oper_char)                             \
  case _oper_char:                                                             \
    oper_phase1_##_oper_name(gbuf, mbuf, height, width, iy, ix);               \
    break;
#define SIM_EXPAND_PHASE_2(_oper_name, _oper_char)                             \
  case _oper_char:                                                             \
    oper_phase2_##_oper_name(gbuf, mbuf, height, width, iy, ix);               \
    break;

static void sim_phase_0(Gbuffer gbuf, Mbuffer mbuf, Usz height, Usz width) {
  for (Usz iy = 0; iy < height; ++iy) {
    Glyph* glyph_row = gbuf + iy * width;
    for (Usz ix = 0; ix < width; ++ix) {
      Glyph c = glyph_row[ix];
      switch (c) { ORCA_OPERATORS(SIM_EXPAND_PHASE_0) }
    }
  }
}

#define SIM_MUTATING_PHASE(_n)                                                 \
  static void sim_phase_##_n(Gbuffer gbuf, Mbuffer mbuf, Usz height,           \
                             Usz width) {                                      \
    for (Usz iy = 0; iy < height; ++iy) {                                      \
      Glyph* glyph_row = gbuf + iy * width;                                    \
      for (Usz ix = 0; ix < width; ++ix) {                                     \
        Glyph c = glyph_row[ix];                                               \
        if (c == '.')                                                          \
          continue;                                                            \
        if (mbuffer_peek(mbuf, height, width, iy, ix) &                        \
            (Mark_flag_lock | Mark_flag_sleep))                                \
          continue;                                                            \
        switch (c) { ORCA_OPERATORS(SIM_EXPAND_PHASE_##_n) }                   \
      }                                                                        \
    }                                                                          \
  }

SIM_MUTATING_PHASE(1)
SIM_MUTATING_PHASE(2)

void orca_run(Gbuffer gbuf, Mbuffer mbuf, Usz height, Usz width) {
  mbuffer_clear(mbuf, height, width);
  sim_phase_0(gbuf, mbuf, height, width);
  sim_phase_1(gbuf, mbuf, height, width);
  sim_phase_2(gbuf, mbuf, height, width);
}
