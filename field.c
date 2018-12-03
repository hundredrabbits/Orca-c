#include "field.h"
#include <ctype.h>

void field_init(Field* f) {
  f->buffer = NULL;
  f->height = 0;
  f->width = 0;
}

void field_init_fill(Field* f, Usz height, Usz width, Glyph fill_char) {
  assert(height <= ORCA_Y_MAX && width <= ORCA_X_MAX);
  Usz num_cells = height * width;
  f->buffer = malloc(num_cells * sizeof(Glyph));
  memset(f->buffer, fill_char, num_cells);
  f->height = (U16)height;
  f->width = (U16)width;
}

void field_resize_raw(Field* f, Usz height, Usz width) {
  assert(height <= ORCA_Y_MAX && width <= ORCA_X_MAX);
  Usz cells = height * width;
  f->buffer = realloc(f->buffer, cells * sizeof(Glyph));
  f->height = (U16)height;
  f->width = (U16)width;
}

void field_resize_raw_if_necessary(Field* field, Usz height, Usz width) {
  if (field->height != height || field->width != width) {
    field_resize_raw(field, height, width);
  }
}

void field_deinit(Field* f) { free(f->buffer); }

void field_copy(Field* src, Field* dest) {
  field_resize_raw_if_necessary(dest, src->height, src->width);
  field_copy_subrect(src, dest, 0, 0, 0, 0, src->height, src->width);
}

void field_copy_subrect(Field* src, Field* dest, Usz src_y, Usz src_x,
                        Usz dest_y, Usz dest_x, Usz height, Usz width) {
  Usz src_height = src->height;
  Usz src_width = src->width;
  Usz dest_height = dest->height;
  Usz dest_width = dest->width;
  if (src_height <= src_y || src_width <= src_x || dest_height <= dest_y ||
      dest_width <= dest_x)
    return;
  Usz ny_0 = src_height - src_y;
  Usz ny_1 = dest_height - dest_y;
  Usz ny = height;
  if (ny_0 < ny)
    ny = ny_0;
  if (ny_1 < ny)
    ny = ny_1;
  if (ny == 0)
    return;
  Usz row_copy_0 = src_width - src_x;
  Usz row_copy_1 = dest_width - dest_x;
  Usz row_copy = width;
  if (row_copy_0 < row_copy)
    row_copy = row_copy_0;
  if (row_copy_1 < row_copy)
    row_copy = row_copy_1;
  Usz copy_bytes = row_copy * sizeof(Glyph);
  Glyph* src_p = src->buffer + src_y * src_width + src_x;
  Glyph* dest_p = dest->buffer + dest_y * dest_width + dest_x;
  Usz src_stride;
  Usz dest_stride;
  if (src_y >= dest_y) {
    src_stride = src_width;
    dest_stride = dest_width;
  } else {
    src_p += (ny - 1) * src_width;
    dest_p += (ny - 1) * dest_width;
    src_stride = -src_width;
    dest_stride = -dest_width;
  }
  Usz iy = 0;
  for (;;) {
    memmove(dest_p, src_p, copy_bytes);
    ++iy;
    if (iy == ny)
      break;
    src_p += src_stride;
    dest_p += dest_stride;
  }
}

void field_fill_subrect(Field* f, Usz y, Usz x, Usz height, Usz width,
                        Glyph fill_char) {
  Usz f_height = f->height;
  Usz f_width = f->width;
  if (y >= f_height || x >= f_width)
    return;
  Usz rows_0 = f_height - y;
  Usz rows = height;
  if (rows_0 < rows)
    rows = rows_0;
  if (rows == 0)
    return;
  Usz columns_0 = f_width - x;
  Usz columns = width;
  if (columns_0 < columns)
    columns = columns_0;
  Usz fill_bytes = columns * sizeof(Glyph);
  Glyph* p = f->buffer + y * f_width + x;
  Usz iy = 0;
  for (;;) {
    memset(p, fill_char, fill_bytes);
    ++iy;
    if (iy == rows)
      break;
    p += f_width;
  }
}

Glyph field_peek(Field* f, Usz y, Usz x) {
  Usz f_height = f->height;
  Usz f_width = f->width;
  assert(y < f_height && x < f_width);
  if (y >= f_height || x >= f_width)
    return '\0';
  return f->buffer[y * f_width + x];
}

Glyph field_peek_relative(Field* f, Usz y, Usz x, Isz offs_y, Isz offs_x) {
  Isz f_height = f->height;
  Isz f_width = f->width;
  Isz y0 = (Isz)y + offs_y;
  Isz x0 = (Isz)x + offs_x;
  if (y0 >= f_height || x0 >= f_width || y0 < 0 || x0 < 0)
    return '.';
  return f->buffer[y0 * f_width + x0];
}

void field_poke(Field* f, Usz y, Usz x, Glyph glyph) {
  Usz f_height = f->height;
  Usz f_width = f->width;
  assert(y < f_height && x < f_width);
  if (y >= f_height || x >= f_width)
    return;
  f->buffer[y * f_width + x] = glyph;
}

void field_poke_relative(Field* f, Usz y, Usz x, Isz offs_y, Isz offs_x,
                         Glyph glyph) {
  Isz f_height = f->height;
  Isz f_width = f->width;
  Isz y0 = (Isz)y + offs_y;
  Isz x0 = (Isz)x + offs_x;
  if (y0 >= f_height || x0 >= f_width || y0 < 0 || x0 < 0)
    return;
  f->buffer[y0 * f_width + x0] = glyph;
}

static inline bool glyph_char_is_valid(char c) { return c >= '#' && c <= '~'; }

void field_fput(Field* f, FILE* stream) {
  enum { Column_buffer_count = 4096 };
  char out_buffer[Column_buffer_count];
  Usz f_height = f->height;
  Usz f_width = f->width;
  Glyph* f_buffer = f->buffer;
  if (f_width > Column_buffer_count - 2)
    return;
  for (Usz iy = 0; iy < f_height; ++iy) {
    Glyph* row_p = f_buffer + f_width * iy;
    for (Usz ix = 0; ix < f_width; ++ix) {
      char c = row_p[ix];
      out_buffer[ix] = glyph_char_is_valid(c) ? c : '!';
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
  Usz first_row_columns = 0;
  Usz rows = 0;
  for (;;) {
    char* s = fgets(buf, Bufsize, file);
    if (s == NULL)
      break;
    if (rows == ORCA_Y_MAX) {
      fclose(file);
      return Field_load_error_too_many_rows;
    }
    Usz len = strlen(buf);
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
    Glyph* rowbuff = field->buffer + first_row_columns * rows;
    for (Usz i = 0; i < len; ++i) {
      char c = buf[i];
      rowbuff[i] = glyph_char_is_valid(c) ? c : '.';
    }
    ++rows;
  }
  fclose(file);
  return Field_load_error_ok;
}
