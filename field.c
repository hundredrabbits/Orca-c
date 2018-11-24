#include "field.h"
#include <assert.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

void field_init_zeros(Field* f, U32 height, U32 width) {
  size_t num_cells = height * width;
  f->buffer = calloc(num_cells, sizeof(Term));
  f->height = height;
  f->width = width;
}

void field_init_fill(Field* f, U32 height, U32 width, Term fill_char) {
  size_t num_cells = height * width;
  f->buffer = malloc(num_cells * sizeof(Term));
  memset(f->buffer, fill_char, num_cells);
  f->height = height;
  f->width = width;
}

void field_resize_raw(Field* f, U32 height, U32 width) {
  size_t cells = height * width;
  f->buffer = realloc(f->buffer, cells * sizeof(Term));
  f->height = height;
  f->width = width;
}

void field_deinit(Field* f) {
  assert(f->buffer != NULL);
  free(f->buffer);
#ifndef NDEBUG
  f->buffer = NULL;
#endif
}

void field_copy_subrect(Field* src, Field* dest, U32 src_y, U32 src_x,
                        U32 dest_y, U32 dest_x, U32 height, U32 width) {
  size_t src_height = src->height;
  size_t src_width = src->width;
  size_t dest_height = dest->height;
  size_t dest_width = dest->width;
  if (src_height <= src_y || src_width <= src_x || dest_height <= dest_y ||
      dest_width <= dest_x)
    return;
  size_t ny_0 = src_height - src_y;
  size_t ny_1 = dest_height - dest_y;
  size_t ny = height;
  if (ny_0 < ny)
    ny = ny_0;
  if (ny_1 < ny)
    ny = ny_1;
  if (ny == 0)
    return;
  size_t row_copy_0 = src_width - src_x;
  size_t row_copy_1 = dest_width - dest_x;
  size_t row_copy = width;
  if (row_copy_0 < row_copy)
    row_copy = row_copy_0;
  if (row_copy_1 < row_copy)
    row_copy = row_copy_1;
  size_t copy_bytes = row_copy * sizeof(Term);
  Term* src_p = src->buffer + src_y * src_width + src_x;
  Term* dest_p = dest->buffer + dest_y * dest_width + dest_x;
  size_t src_stride;
  size_t dest_stride;
  if (src_y >= dest_y) {
    src_stride = src_width;
    dest_stride = dest_width;
  } else {
    src_p += (ny - 1) * src_width;
    dest_p += (ny - 1) * dest_width;
    src_stride = -src_width;
    dest_stride = -dest_width;
  }
  size_t iy = 0;
  for (;;) {
    memmove(dest_p, src_p, copy_bytes);
    ++iy;
    if (iy == ny)
      break;
    src_p += src_stride;
    dest_p += dest_stride;
  }
}

void field_fill_subrect(Field* f, U32 y, U32 x, U32 height, U32 width,
                        Term fill_char) {
  size_t f_height = f->height;
  size_t f_width = f->width;
  if (y >= f_height || x >= f_width)
    return;
  size_t rows_0 = f_height - y;
  size_t rows = height;
  if (rows_0 < rows)
    rows = rows_0;
  if (rows == 0)
    return;
  size_t columns_0 = f_width - x;
  size_t columns = width;
  if (columns_0 < columns)
    columns = columns_0;
  size_t fill_bytes = columns * sizeof(Term);
  Term* p = f->buffer + y * f_width + x;
  size_t iy = 0;
  for (;;) {
    memset(p, fill_char, fill_bytes);
    ++iy;
    if (iy == rows)
      break;
    p += f_width;
  }
}

Term field_peek(Field* f, U32 y, U32 x) {
  size_t f_height = f->height;
  size_t f_width = f->width;
  assert(y < f_height && x < f_width);
  if (y >= f_height || x >= f_width) return '\0';
  return f->buffer[y * f_width + x];
}

void field_poke(Field* f, U32 y, U32 x, Term term) {
  size_t f_height = f->height;
  size_t f_width = f->width;
  assert(y < f_height && x < f_width);
  if (y >= f_height || x >= f_width) return;
  f->buffer[y * f_width + x] = term;
}

void field_debug_draw(Field* f, int offset_y, int offset_x) {
  enum { Line_buffer_count = 4096 };
  chtype line_buffer[Line_buffer_count];
  size_t f_height = f->height;
  size_t f_width = f->width;
  Term* f_buffer = f->buffer;
  if (f_width > Line_buffer_count)
    return;
  for (size_t iy = 0; iy < f_height; ++iy) {
    Term* row_p = f_buffer + f_width * iy;
    for (size_t ix = 0; ix < f_width; ++ix) {
      line_buffer[ix] = (chtype)row_p[ix];
    }
    move(iy + offset_y, offset_x);
    addchnstr(line_buffer, (int)f_width);
  }
}
