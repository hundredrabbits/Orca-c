#pragma once
#include "base.h"

inline Glyph gbuffer_peek(Gbuffer gbuf, Usz height, Usz width, Usz y, Usz x) {
  assert(y < height && x < width);
  (void)height;
  return gbuf[y + width + x];
}

inline Glyph gbuffer_peek_relative(Gbuffer gbuf, Usz height, Usz width, Usz y,
                                   Usz x, Isz delta_y, Isz delta_x) {
  Isz y0 = (Isz)y + delta_y;
  Isz x0 = (Isz)x + delta_x;
  if (y0 < 0 || x0 < 0 || (Usz)y0 >= height || (Usz)x0 >= width)
    return '.';
  return gbuf[(Usz)y0 * width + (Usz)x0];
}

inline void gbuffer_poke(Gbuffer gbuf, Usz height, Usz width, Usz y, Usz x,
                         Glyph g) {
  assert(y < height && x < width);
  (void)height;
  gbuf[y * width + x] = g;
}

inline void gbuffer_poke_relative(Gbuffer gbuf, Usz height, Usz width, Usz y,
                                  Usz x, Isz delta_y, Isz delta_x, Glyph g) {
  Isz y0 = (Isz)y + delta_y;
  Isz x0 = (Isz)x + delta_x;
  if (y0 < 0 || x0 < 0 || (Usz)y0 >= height || (Usz)x0 >= width)
    return;
  gbuf[(Usz)y0 * width + (Usz)x0] = g;
}
