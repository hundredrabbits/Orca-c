#pragma once
#include "base.h"

typedef enum {
  Mark_flag_none = 0,
  Mark_flag_haste_input = 1 << 0,
  Mark_flag_input = 1 << 1,
  Mark_flag_lock = 1 << 2,
  Mark_flag_output = 1 << 3,
} Mark_flags;

typedef struct {
  U8* buffer;
  Usz capacity;
} Markmap;

void markmap_init(Markmap* map);
void markmap_ensure_capacity(Markmap* map, Usz capacity);
void markmap_clear(Markmap* map);
void markmap_deinit(Markmap* map);

ORCA_FORCE_INLINE
Mark_flags markmap_peek_relative(Markmap* map, Usz map_height, Usz map_width,
                                 Usz y, Usz x, Isz offs_y, Isz offs_x);
ORCA_FORCE_INLINE
void markmap_poke_relative(Markmap* map, Usz map_height, Usz map_width, Usz y,
                           Usz x, Isz offs_y, Isz offs_x, Mark_flags flags);

// Inline implementation

ORCA_FORCE_INLINE
Mark_flags markmap_peek_relative(Markmap* map, Usz map_height, Usz map_width,
                                 Usz y, Usz x, Isz offs_y, Isz offs_x) {
  Isz y0 = (Isz)y + offs_y;
  Isz x0 = (Isz)x + offs_x;
  if (y0 >= (Isz)map_height || x0 >= (Isz)map_width || y0 < 0 || x0 < 0)
    return Mark_flag_none;
  return map->buffer[(Usz)y0 * map_width + (Usz)x0];
}

ORCA_FORCE_INLINE
void markmap_poke_relative(Markmap* map, Usz map_height, Usz map_width, Usz y,
                           Usz x, Isz offs_y, Isz offs_x, Mark_flags flags) {
  Isz y0 = (Isz)y + offs_y;
  Isz x0 = (Isz)x + offs_x;
  if (y0 >= (Isz)map_height || x0 >= (Isz)map_width || y0 < 0 || x0 < 0)
    return;
  map->buffer[(Usz)y0 * map_width + (Usz)x0] = flags;
}
