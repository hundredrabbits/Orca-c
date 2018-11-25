#pragma once
#include "base.h"

void field_init(Field* f);
void field_init_fill(Field* f, USz height, USz width, Term fill_char);
void field_resize_raw(Field* f, USz height, USz width);
void field_deinit(Field* f);
void field_copy_subrect(Field* src, Field* dest, USz src_y, USz src_x,
                        USz dest_y, USz dest_x, USz height, USz width);
void field_fill_subrect(Field* f, USz y, USz x, USz height, USz width,
                        Term fill_char);
Term field_peek(Field* f, USz y, USz x);
Term field_peek_relative(Field* f, USz y, USz x, ISz offs_y, ISz offs_x);
void field_poke(Field* f, USz y, USz x, Term term);
void field_poke_relative(Field* f, USz y, USz x, ISz offs_y, ISz offs_x,
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
