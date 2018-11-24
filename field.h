#pragma once
#include "base.h"

typedef struct {
  Term* buffer;
  U32 height;
  U32 width;
} Field;

void field_init_zeros(Field* f, U32 height, U32 width);
void field_init_fill(Field* f, U32 height, U32 width, Term fill_char);
void field_resize_raw(Field* f, U32 height, U32 width);
void field_deinit(Field* f);
void field_copy_subrect(Field* src, Field* dest, U32 src_y, U32 src_x,
                        U32 dest_y, U32 dest_x, U32 height, U32 width);
void field_fill_subrect(Field* f, U32 y, U32 x, U32 height, U32 width,
                        Term fill_char);
Term field_peek(Field* f, U32 y, U32 x);
void field_poke(Field* f, U32 y, U32 x, Term term);
void field_debug_draw(Field* f, int offset_y, int offset_x);
