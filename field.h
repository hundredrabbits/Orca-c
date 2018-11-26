#pragma once
#include "base.h"

void field_init(Field* field);
void field_init_fill(Field* field, Usz height, Usz width, Glyph fill_char);
void field_resize_raw(Field* field, Usz height, Usz width);
void field_deinit(Field* field);
void field_copy_subrect(Field* src, Field* dest, Usz src_y, Usz src_x,
                        Usz dest_y, Usz dest_x, Usz height, Usz width);
void field_fill_subrect(Field* field, Usz y, Usz x, Usz height, Usz width,
                        Glyph fill_char);
Glyph field_peek(Field* field, Usz y, Usz x);
Glyph field_peek_relative(Field* field, Usz y, Usz x, Isz offs_y, Isz offs_x);
void field_poke(Field* field, Usz y, Usz x, Glyph glyph);
void field_poke_relative(Field* field, Usz y, Usz x, Isz offs_y, Isz offs_x,
                         Glyph glyph);

void field_fput(Field* field, FILE* stream);

typedef enum {
  Field_load_error_ok = 0,
  Field_load_error_cant_open_file = 1,
  Field_load_error_too_many_columns = 2,
  Field_load_error_too_many_rows = 3,
  Field_load_error_no_rows_read = 4,
  Field_load_error_not_a_rectangle = 5,
} Field_load_error;

Field_load_error field_load_file(char const* filepath, Field* field);

inline Glyph gbuffer_peek_relative(Field_buffer gbuffer, Usz height, Usz width,
                                   Usz y, Usz x, Isz delta_y, Isz delta_x) {
  Isz y0 = (Isz)y + delta_y;
  Isz x0 = (Isz)x + delta_x;
  if (y0 < 0 || x0 < 0 || (Usz)y0 >= height || (Usz)x0 >= width)
    return '.';
  return gbuffer[(Usz)y0 * width + (Usz)x0];
}
