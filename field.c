#include "field.h"
#include <ctype.h>

void field_init(Field* f) {
  f->buffer = NULL;
  f->height = 0;
  f->width = 0;
}

void field_init_fill(Field* f, size_t height, size_t width, Term fill_char) {
  assert(height <= ORCA_Y_MAX && width <= ORCA_X_MAX);
  size_t num_cells = height * width;
  f->buffer = malloc(num_cells * sizeof(Term));
  memset(f->buffer, fill_char, num_cells);
  f->height = (U16)height;
  f->width = (U16)width;
}

void field_resize_raw(Field* f, size_t height, size_t width) {
  assert(height <= ORCA_Y_MAX && width <= ORCA_X_MAX);
  size_t cells = height * width;
  f->buffer = realloc(f->buffer, cells * sizeof(Term));
  f->height = (U16)height;
  f->width = (U16)width;
}

void field_deinit(Field* f) { free(f->buffer); }

void field_copy_subrect(Field* src, Field* dest, size_t src_y, size_t src_x,
                        size_t dest_y, size_t dest_x, size_t height,
                        size_t width) {
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

void field_fill_subrect(Field* f, size_t y, size_t x, size_t height,
                        size_t width, Term fill_char) {
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

Term field_peek(Field* f, size_t y, size_t x) {
  size_t f_height = f->height;
  size_t f_width = f->width;
  assert(y < f_height && x < f_width);
  if (y >= f_height || x >= f_width)
    return '\0';
  return f->buffer[y * f_width + x];
}

Term field_peek_relative(Field* f, size_t y, size_t x, ssize_t offs_y,
                         ssize_t offs_x) {
  ssize_t f_height = f->height;
  ssize_t f_width = f->width;
  ssize_t y0 = (ssize_t)y + (ssize_t)offs_y;
  ssize_t x0 = (ssize_t)x + (ssize_t)offs_x;
  if (y0 >= f_height || x0 >= f_width || y0 < 0 || x0 < 0)
    return '.';
  return f->buffer[y0 * f_width + x0];
}

void field_poke(Field* f, size_t y, size_t x, Term term) {
  size_t f_height = f->height;
  size_t f_width = f->width;
  assert(y < f_height && x < f_width);
  if (y >= f_height || x >= f_width)
    return;
  f->buffer[y * f_width + x] = term;
}

void field_poke_relative(Field* f, size_t y, size_t x, ssize_t offs_y,
                         ssize_t offs_x, Term term) {
  ssize_t f_height = f->height;
  ssize_t f_width = f->width;
  ssize_t y0 = (ssize_t)y + (ssize_t)offs_y;
  ssize_t x0 = (ssize_t)x + (ssize_t)offs_x;
  if (y0 >= f_height || x0 >= f_width || y0 < 0 || x0 < 0)
    return;
  f->buffer[y0 * f_width + x0] = term;
}

static inline bool term_char_is_valid(char c) { return c >= '#' && c <= '~'; }

void field_fput(Field* f, FILE* stream) {
  enum { Column_buffer_count = 4096 };
  char out_buffer[Column_buffer_count];
  size_t f_height = f->height;
  size_t f_width = f->width;
  Term* f_buffer = f->buffer;
  if (f_width > Column_buffer_count - 2)
    return;
  for (size_t iy = 0; iy < f_height; ++iy) {
    Term* row_p = f_buffer + f_width * iy;
    for (size_t ix = 0; ix < f_width; ++ix) {
      char c = row_p[ix];
      out_buffer[ix] = term_char_is_valid(c) ? c : '!';
    }
    out_buffer[f_width] = '\n';
    out_buffer[f_width + 1] = '\0';
    fputs(out_buffer, stream);
  }
}

Field_load_error field_load_file(char const* filepath, Field* field) {
  FILE* file = fopen(filepath, "r");
  if (file == NULL) {
    return Field_load_error_cant_open_file;
  }
  enum { Bufsize = 4096 };
  char buf[Bufsize];
  size_t first_row_columns = 0;
  size_t rows = 0;
  for (;;) {
    char* s = fgets(buf, Bufsize, file);
    if (s == NULL)
      break;
    if (rows == ORCA_Y_MAX) {
      fclose(file);
      return Field_load_error_too_many_rows;
    }
    size_t len = strlen(buf);
    if (len == Bufsize - 1 && buf[len - 1] != '\n' && !feof(file)) {
      fclose(file);
      return Field_load_error_too_many_columns;
    }
    for (;;) {
      if (len == 0)
        break;
      if (!isspace(buf[len - 1]))
        break;
      --len;
    }
    if (len == 0)
      continue;
    if (len >= ORCA_X_MAX) {
      fclose(file);
      return Field_load_error_too_many_columns;
    }
    // quick hack until we use a proper scanner
    if (rows == 0) {
      first_row_columns = len;
    } else if (len != first_row_columns) {
      fclose(file);
      return Field_load_error_not_a_rectangle;
    }
    field_resize_raw(field, rows + 1, first_row_columns);
    Term* rowbuff = field->buffer + first_row_columns * rows;
    for (size_t i = 0; i < len; ++i) {
      char c = buf[i];
      rowbuff[i] = term_char_is_valid(c) ? c : '.';
    }
    ++rows;
  }
  fclose(file);
  return Field_load_error_ok;
}
