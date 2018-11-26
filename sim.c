#include "field.h"
#include "mark.h"
#include "sim.h"

//////// Utilities

static Glyph const indexed_glyphs[] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd',
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r',
    's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '.', '*', ':', ';', '#',
};

enum { Glyphs_array_num = sizeof indexed_glyphs };

static inline Usz index_of_glyph(Glyph c) {
  for (Usz i = 0; i < Glyphs_array_num; ++i) {
    if (indexed_glyphs[i] == c)
      return i;
  }
  return SIZE_MAX;
}

static inline Glyph glyph_lowered(Glyph c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c - ('a' - 'A')) : c;
}

// Always returns 0 through (sizeof indexed_glyphs) - 1, and works on
// capitalized glyphs as well. The index of the lower-cased glyph is returned
// if the glyph is capitalized.
static inline Usz semantic_index_of_glyph(Glyph c) {
  Glyph c0 = glyph_lowered(c);
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

static inline bool oper_has_neighboring_bang(Field_buffer gbuf, Usz h, Usz w,
                                             Usz y, Usz x) {
  return gbuffer_peek_relative(gbuf, h, w, y, x, 0, 1) == '*' ||
         gbuffer_peek_relative(gbuf, h, w, y, x, 0, -1) == '*' ||
         gbuffer_peek_relative(gbuf, h, w, y, x, 1, 0) == '*' ||
         gbuffer_peek_relative(gbuf, h, w, y, x, -1, 0) == '*';
}

static inline void
oper_move_relative_or_explode(Field_buffer field_buffer, Markmap_buffer markmap,
                              Usz field_height, Usz field_width, Glyph moved,
                              Usz y, Usz x, Isz delta_y, Isz delta_x) {
  Isz y0 = (Isz)y + delta_y;
  Isz x0 = (Isz)x + delta_x;
  if (y0 >= (Isz)field_height || x0 >= (Isz)field_width || y0 < 0 || x0 < 0) {
    field_buffer[y * field_width + x] = '*';
    return;
  }
  Glyph* at_dest = field_buffer + (Usz)y0 * field_width + (Usz)x0;
  if (*at_dest != '.') {
    field_buffer[y * field_width + x] = '*';
    markmap_poke_flags_or(markmap, field_height, field_width, y, x,
                          Mark_flag_sleep);
    return;
  }
  *at_dest = moved;
  markmap_poke_flags_or(markmap, field_height, field_width, (Usz)y0, (Usz)x0,
                        Mark_flag_sleep);
  field_buffer[y * field_width + x] = '.';
}

#define ORCA_EXPAND_OPER_CHARS(_oper_name, _oper_char)                         \
  Orca_oper_char_##_oper_name = _oper_char,
#define ORCA_DEFINE_OPER_CHARS(_defs)                                          \
  enum Orca_oper_chars { _defs(ORCA_EXPAND_OPER_CHARS) };
#define ORCA_DECLARE_OPERATORS(_defs) ORCA_DEFINE_OPER_CHARS(_defs)

#define OPER_PHASE_N(_phase_number, _oper_name)                                \
  static inline void oper_phase##_phase_number##_##_oper_name(                 \
      Field* field, Markmap_buffer markmap, Usz y, Usz x) {                    \
    (void)field;                                                               \
    (void)markmap;                                                             \
    (void)y;                                                                   \
    (void)x;                                                                   \
    enum { This_oper_char = Orca_oper_char_##_oper_name };

#define OPER_PHASE_0(_oper_name) OPER_PHASE_N(0, _oper_name)
#define OPER_PHASE_1(_oper_name) OPER_PHASE_N(1, _oper_name)
#define OPER_PHASE_2(_oper_name) OPER_PHASE_N(2, _oper_name)
#define OPER_END }

#define OPER_POKE_ABSOLUTE(_y, _x, _glyph) field_poke(field, _y, _x, _glyph)
#define OPER_PEEK_RELATIVE(_delta_y, _delta_x)                                 \
  field_peek_relative(field, y, x, _delta_y, _delta_x)
#define OPER_POKE_RELATIVE(_delta_y, _delta_x, _glyph)                         \
  field_poke_relative(field, y, x, _delta_x, _delta_y, _glyph)
#define OPER_POKE_SELF(_glyph) OPER_POKE_ABSOLUTE(y, x, _glyph)

#define OPER_REQUIRE_BANG()                                                    \
  if (!oper_has_neighboring_bang(field->buffer, field->height, field->width,   \
                                 y, x))                                        \
    return;

#define OPER_MOVE_OR_EXPLODE(_delta_y, _delta_x)                               \
  oper_move_relative_or_explode(field->buffer, markmap, field->height,         \
                                field->width, This_oper_char, y, x, _delta_y,  \
                                _delta_x)

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
  _(bang, '*')                                                                 \
  _(add, 'a')                                                                  \
  _(North, 'N')                                                                \
  _(East, 'E')                                                                 \
  _(South, 'S')                                                                \
  _(West, 'W')                                                                 \
  _(north, 'n')                                                                \
  _(east, 'e')                                                                 \
  _(south, 's')                                                                \
  _(west, 'w')                                                                 \
  _(modulo, 'm')

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

OPER_PHASE_0(add)
OPER_END
OPER_PHASE_1(add)
OPER_END
OPER_PHASE_2(add)
  Glyph inp0 = OPER_PEEK_RELATIVE(0, 1);
  Glyph inp1 = OPER_PEEK_RELATIVE(0, 2);
  if (inp0 != '.' && inp1 != '.') {
    Glyph g = glyphs_sum(inp0, inp1);
    OPER_POKE_RELATIVE(1, 0, g);
  }
OPER_END

OPER_PHASE_0(modulo)
OPER_END
OPER_PHASE_1(modulo)
OPER_END
OPER_PHASE_2(modulo)
  Glyph inp0 = OPER_PEEK_RELATIVE(0, 1);
  Glyph inp1 = OPER_PEEK_RELATIVE(0, 2);
  if (inp0 != '.' && inp1 != '.') {
    Glyph g = glyphs_mod(inp0, inp1);
    OPER_POKE_RELATIVE(1, 0, g);
  }
OPER_END

OPER_PHASE_0(bang)
OPER_END
OPER_PHASE_1(bang)
  OPER_POKE_SELF('.');
OPER_END
OPER_PHASE_2(bang)
OPER_END

void orca_run(Field* field, Markmap_buffer markmap) {
  Usz ny = field->height;
  Usz nx = field->width;
  markmap_clear(markmap, ny, nx);
  Glyph* field_buffer = field->buffer;
  // Phase 0
  for (Usz iy = 0; iy < ny; ++iy) {
    Glyph* glyph_row = field_buffer + iy * nx;
    for (Usz ix = 0; ix < nx; ++ix) {
      Glyph c = glyph_row[ix];
      if (markmap_peek(markmap, ny, nx, iy, ix) & Mark_flag_sleep)
        continue;
      switch (c) {
#define X(_oper_name, _oper_char)                                              \
  case _oper_char:                                                             \
    oper_phase0_##_oper_name(field, markmap, iy, ix);                          \
    break;
        ORCA_OPERATORS(X)
#undef X
      }
    }
  }
  // Phase 1
  for (Usz iy = 0; iy < ny; ++iy) {
    Glyph* glyph_row = field_buffer + iy * nx;
    for (Usz ix = 0; ix < nx; ++ix) {
      if (markmap_peek(markmap, ny, nx, iy, ix) & Mark_flag_sleep)
        continue;
      Glyph c = glyph_row[ix];
      switch (c) {
#define X(_oper_name, _oper_char)                                              \
  case _oper_char:                                                             \
    oper_phase1_##_oper_name(field, markmap, iy, ix);                          \
    break;
        ORCA_OPERATORS(X)
#undef X
      }
    }
  }
}
