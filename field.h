#pragma once
#include "base.h"

void field_init(Field* field);
void field_init_fill(Field* field, Usz height, Usz width, Glyph fill_char);
void field_resize_raw(Field* field, Usz height, Usz width);
void field_resize_raw_if_necessary(Field* field, Usz height, Usz width);
void field_deinit(Field* field);
void field_copy(Field* src, Field* dest);
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
