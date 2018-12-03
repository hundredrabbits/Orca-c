#include "gbuffer.h"

void gbuffer_copy_subrect(Glyph* src, Glyph* dest, Usz src_height,
                          Usz src_width, Usz dest_height, Usz dest_width,
                          Usz src_y, Usz src_x, Usz dest_y, Usz dest_x,
                          Usz height, Usz width) {
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
  Glyph* src_p = src + src_y * src_width + src_x;
  Glyph* dest_p = dest + dest_y * dest_width + dest_x;
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
