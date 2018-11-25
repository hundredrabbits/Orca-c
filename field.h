#pragma once
#include "base.h"

void field_init(Field* f);
void field_init_fill(Field* f, Usz height, Usz width, Term fill_char);
void field_resize_raw(Field* f, Usz height, Usz width);
void field_deinit(Field* f);
void field_copy_subrect(Field* src, Field* dest, Usz src_y, Usz src_x,
                        Usz dest_y, Usz dest_x, Usz height, Usz width);
void field_fill_subrect(Field* f, Usz y, Usz x, Usz height, Usz width,
                        Term fill_char);
Term field_peek(Field* f, Usz y, Usz x);
Term field_peek_relative(Field* f, Usz y, Usz x, Isz offs_y, Isz offs_x);
void field_poke(Field* f, Usz y, Usz x, Term term);
void field_poke_relative(Field* f, Usz y, Usz x, Isz offs_y, Isz offs_x,
                         Term term);

void field_fput(Field* f, FILE* stream);

typedef enum {
  Field_load_error_ok = 0,
  Field_load_error_cant_open_file = 1,
  Field_load_error_too_many_columns = 2,
  Field_load_error_too_many_rows = 3,
  Field_load_error_no_rows_read = 4,
  Field_load_error_not_a_rectangle = 5,
} Field_load_error;

Field_load_error field_load_file(char const* filepath, Field* field);
