#include "field.h"
#include "sim.h"

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

static inline void act_a(Field* f, Usz y, Usz x) {
  Glyph inp0 = field_peek_relative(f, y, x, 0, 1);
  Glyph inp1 = field_peek_relative(f, y, x, 0, 2);
  if (inp0 != '.' && inp1 != '.') {
    Glyph t = glyphs_sum(inp0, inp1);
    field_poke_relative(f, y, x, 1, 0, t);
  }
}

static inline void act_m(Field* f, Usz y, Usz x) {
  Glyph inp0 = field_peek_relative(f, y, x, 0, 1);
  Glyph inp1 = field_peek_relative(f, y, x, 0, 2);
  if (inp0 != '.' && inp1 != '.') {
    Glyph t = glyphs_mod(inp0, inp1);
    field_poke_relative(f, y, x, 1, 0, t);
  }
}

void orca_run(Field* f) {
  Usz ny = f->height;
  Usz nx = f->width;
  Glyph* f_buffer = f->buffer;
  for (Usz iy = 0; iy < ny; ++iy) {
    Glyph* row = f_buffer + iy * nx;
    for (Usz ix = 0; ix < nx; ++ix) {
      Glyph c = row[ix];
      switch (c) {
      case 'a':
        act_a(f, iy, ix);
        break;
      case 'm':
        act_m(f, iy, ix);
        break;
      }
    }
  }
}
