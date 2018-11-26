#pragma once
#include "base.h"

inline Glyph gbuffer_peek_relative(Gbuffer gbuffer, Usz height, Usz width,
                                   Usz y, Usz x, Isz delta_y, Isz delta_x) {
  Isz y0 = (Isz)y + delta_y;
  Isz x0 = (Isz)x + delta_x;
  if (y0 < 0 || x0 < 0 || (Usz)y0 >= height || (Usz)x0 >= width)
    return '.';
  return gbuffer[(Usz)y0 * width + (Usz)x0];
}
