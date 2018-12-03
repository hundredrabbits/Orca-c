#pragma once
#include "base.h"

ORCA_PURE static inline Glyph gbuffer_peek(Gbuffer gbuf, Usz height, Usz width,
                                           Usz y, Usz x) {
  assert(y < height && x < width);
  (void)height;
  return gbuf[y + width + x];
}

ORCA_PURE static inline Glyph gbuffer_peek_relative(Gbuffer gbuf, Usz height,
                                                    Usz width, Usz y, Usz x,
                                                    Isz delta_y, Isz delta_x) {
  Isz y0 = (Isz)y + delta_y;
  Isz x0 = (Isz)x + delta_x;
  if (y0 < 0 || x0 < 0 || (Usz)y0 >= height || (Usz)x0 >= width)
    return '.';
  return gbuf[(Usz)y0 * width + (Usz)x0];
}

static inline void gbuffer_poke(Gbuffer gbuf, Usz height, Usz width, Usz y,
                                Usz x, Glyph g) {
  assert(y < height && x < width);
  (void)height;
  gbuf[y * width + x] = g;
}

static inline void gbuffer_poke_relative(Gbuffer gbuf, Usz height, Usz width,
                                         Usz y, Usz x, Isz delta_y, Isz delta_x,
                                         Glyph g) {
  Isz y0 = (Isz)y + delta_y;
  Isz x0 = (Isz)x + delta_x;
  if (y0 < 0 || x0 < 0 || (Usz)y0 >= height || (Usz)x0 >= width)
    return;
  gbuf[(Usz)y0 * width + (Usz)x0] = g;
}

ORCA_FORCE_NO_INLINE
void gbuffer_copy_subrect(Glyph* src, Glyph* dest, Usz src_grid_h,
                          Usz src_grid_w, Usz dest_grid_h, Usz dest_grid_w,
                          Usz src_y, Usz src_x, Usz dest_y, Usz dest_x,
                          Usz height, Usz width);
