#include "field.h"
#include <ctype.h>

void field_init(Field* f) {
  f->buffer = NULL;
  f->height = 0;
  f->width = 0;
}

void field_init_fill(Field* f, USz height, USz width, Term fill_char) {
  assert(height <= ORCA_Y_MAX && width <= ORCA_X_MAX);
  USz num_cells = height * width;
  f->buffer = malloc(num_cells * sizeof(Term));
  memset(f->buffer, fill_char, num_cells);
  f->height = (U16)height;
  f->width = (U16)width;
}

void field_resize_raw(Field* f, USz height, USz width) {
  assert(height <= ORCA_Y_MAX && width <= ORCA_X_MAX);
  USz cells = height * width;
  f->buffer = realloc(f->buffer, cells * sizeof(Term));
  f->height = (U16)height;
  f->width = (U16)width;
}

void field_deinit(Field* f) { free(f->buffer); }

void field_copy_subrect(Field* src, Field* dest, USz src_y, USz src_x,
                        USz dest_y, USz dest_x, USz height, USz width) {
  USz src_height = src->height;
  USz src_width = src->width;
  USz dest_height = dest->height;
  USz dest_width = dest->width;
  if (src_height <= src_y || src_width <= src_x || dest_height <= dest_y ||
      dest_width <= dest_x)
    return;
  USz ny_0 = src_height - src_y;
  USz ny_1 = dest_height - dest_y;
  USz ny = height;
  if (ny_0 < ny)
    ny = ny_0;
  if (ny_1 < ny)
    ny = ny_1;
  if (ny == 0)
    return;
  USz row_copy_0 = src_width - src_x;
  USz row_copy_1 = dest_width - dest_x;
  USz row_copy = width;
  if (row_copy_0 < row_copy)
    row_copy = row_copy_0;
  if (row_copy_1 < row_copy)
    row_copy = row_copy_1;
  USz copy_bytes = row_copy * sizeof(Term);
  Term* src_p = src->buffer + src_y * src_width + src_x;
  Term* dest_p = dest->buffer + dest_y * dest_width + dest_x;
  USz src_stride;
  USz dest_stride;
  if (src_y >= dest_y) {
    src_stride = src_width;
    dest_stride = dest_width;
  } else {
    src_p += (ny - 1) * src_width;
    dest_p += (ny - 1) * dest_width;
    src_stride = -src_width;
    dest_stride = -dest_width;
  }
  USz iy = 0;
  for (;;) {
    memmove(dest_p, src_p, copy_bytes);
    ++iy;
    if (iy == ny)
      break;
    src_p += src_stride;
    dest_p += dest_stride;
  }
}

void field_fill_subrect(Field* f, USz y, USz x, USz height, USz width,
                        Term fill_char) {
  USz f_height = f->height;
  USz f_width = f->width;
  if (y >= f_height || x >= f_width)
    return;
  USz rows_0 = f_height - y;
  USz rows = height;
  if (rows_0 < rows)
    rows = rows_0;
  if (rows == 0)
    return;
  USz columns_0 = f_width - x;
  USz columns = width;
  if (columns_0 < columns)
    columns = columns_0;
  USz fill_bytes = columns * sizeof(Term);
  Term* p = f->buffer + y * f_width + x;
  USz iy = 0;
  for (;;) {
    memset(p, fill_char, fill_bytes);
    ++iy;
    if (iy == rows)
      break;
    p += f_width;
  }
}

Term field_peek(Field* f, USz y, USz x) {
  USz f_height = f->height;
  USz f_width = f->width;
  assert(y < f_height && x < f_width);
  if (y >= f_height || x >= f_width)
    return '\0';
  return f->buffer[y * f_width + x];
}

Term field_peek_relative(Field* f, USz y, USz x, ISz offs_y, ISz offs_x) {
  ISz f_height = f->height;
  ISz f_width = f->width;
  ISz y0 = (ISz)y + (ISz)offs_y;
  ISz x0 = (ISz)x + (ISz)offs_x;
  if (y0 >= f_height || x0 >= f_width || y0 < 0 || x0 < 0)
    return '.';
  return f->buffer[y0 * f_width + x0];
}

void field_poke(Field* f, USz y, USz x, Term term) {
  USz f_height = f->height;
  USz f_width = f->width;
  assert(y < f_height && x < f_width);
  if (y >= f_height || x >= f_width)
    return;
  f->buffer[y * f_width + x] = term;
}

void field_poke_relative(Field* f, USz y, USz x, ISz offs_y, ISz offs_x,
                         Term term) {
  ISz f_height = f->height;
  ISz f_width = f->width;
  ISz y0 = (ISz)y + (ISz)offs_y;
  ISz x0 = (ISz)x + (ISz)offs_x;
  if (y0 >= f_height || x0 >= f_width || y0 < 0 || x0 < 0)
    return;
  f->buffer[y0 * f_width + x0] = term;
}

static inline bool term_char_is_valid(char c) { return c >= '#' && c <= '~'; }

void field_fput(Field* f, FILE* stream) {
  enum { Column_buffer_count = 4096 };
  char out_buffer[Column_buffer_count];
  USz f_height = f->height;
  USz f_width = f->width;
  Term* f_buffer = f->buffer;
  if (f_width > Column_buffer_count - 2)
    return;
  for (USz iy = 0; iy < f_height; ++iy) {
    Term* row_p = f_buffer + f_width * iy;
    for (USz ix = 0; ix < f_width; ++ix) {
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
  USz first_row_columns = 0;
  USz rows = 0;
  for (;;) {
    char* s = fgets(buf, Bufsize, file);
    if (s == NULL)
      break;
    if (rows == ORCA_Y_MAX) {
      fclose(file);
      return Field_load_error_too_many_rows;
    }
    USz len = strlen(buf);
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
    for (USz i = 0; i < len; ++i) {
      char c = buf[i];
      rowbuff[i] = term_char_is_valid(c) ? c : '.';
    }
    ++rows;
  }
  fclose(file);
  return Field_load_error_ok;
}
