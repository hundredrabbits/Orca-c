#include "field.h"
#include "mark.h"
#include "sim.h"

#define OPER_INLINE static inline

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

static inline void oper_move_relative_or_explode(Glyph* field_buffer,
                                                 Usz field_height,
                                                 Usz field_width, Glyph moved,
                                                 Usz y, Usz x, Isz delta_y,
                                                 Isz delta_x) {
  Isz y0 = (Isz)y + delta_y;
  Isz x0 = (Isz)x + delta_x;
  if (y0 >= (Isz)field_height || x0 >= (Isz)field_width || y0 < 0 || x0 < 0) {
    field_buffer[y * field_width + x] = '*';
    return;
  }
  Glyph* at_dest = field_buffer + (Usz)y0 * field_width + (Usz)x0;
  if (*at_dest != '.') {
    field_buffer[y * field_width + x] = '*';
    return;
  }
  *at_dest = moved;
  field_buffer[y * field_width + x] = '.';
}

static inline void oper_a_phase1(Field* field, Usz y, Usz x) {
  Glyph inp0 = field_peek_relative(field, y, x, 0, 1);
  Glyph inp1 = field_peek_relative(field, y, x, 0, 2);
  if (inp0 != '.' && inp1 != '.') {
    Glyph g = glyphs_sum(inp0, inp1);
    field_poke_relative(field, y, x, 1, 0, g);
  }
}

static inline void oper_E_phase0(Field* field, Usz y, Usz x) {
  oper_move_relative_or_explode(field->buffer, field->height, field->width, 'E',
                                y, x, 0, 1);
}

static inline void oper_m_phase1(Field* field, Usz y, Usz x) {
  Glyph inp0 = field_peek_relative(field, y, x, 0, 1);
  Glyph inp1 = field_peek_relative(field, y, x, 0, 2);
  if (inp0 != '.' && inp1 != '.') {
    Glyph g = glyphs_mod(inp0, inp1);
    field_poke_relative(field, y, x, 1, 0, g);
  }
}

void orca_run(Field* field) {
  Usz ny = field->height;
  Usz nx = field->width;
  Glyph* field_buffer = field->buffer;
  // Phase 0
  for (Usz iy = 0; iy < ny; ++iy) {
    Glyph* row = field_buffer + iy * nx;
    for (Usz ix = 0; ix < nx; ++ix) {
      Glyph c = row[ix];
      switch (c) {
      case 'E':
        oper_E_phase0(field, iy, ix);
        break;
      }
    }
  }
  // Phase 1
  for (Usz iy = 0; iy < ny; ++iy) {
    Glyph* row = field_buffer + iy * nx;
    for (Usz ix = 0; ix < nx; ++ix) {
      Glyph c = row[ix];
      switch (c) {
      case 'a':
        oper_a_phase1(field, iy, ix);
        break;
      case 'm':
        oper_m_phase1(field, iy, ix);
        break;
      }
    }
  }
}

#undef OPER_INLINE
