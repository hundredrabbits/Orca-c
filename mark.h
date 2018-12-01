#pragma once
#include "base.h"

typedef enum {
  Mark_flag_none = 0,
  Mark_flag_input = 1 << 0,
  Mark_flag_output = 1 << 1,
  Mark_flag_haste_input = 1 << 2,
  Mark_flag_lock = 1 << 3,
  Mark_flag_sleep = 1 << 4,
} Mark_flags;

typedef U8* Mbuffer;

typedef struct Markmap_reusable {
  Mbuffer buffer;
  Usz capacity;
} Markmap_reusable;

void markmap_reusable_init(Markmap_reusable* map);
void markmap_reusable_ensure_size(Markmap_reusable* map, Usz height, Usz width);
void markmap_reusable_deinit(Markmap_reusable* map);

void mbuffer_clear(Mbuffer map, Usz height, Usz width);

ORCA_OK_IF_UNUSED
static Mark_flags mbuffer_peek_relative(Mbuffer map, Usz map_height,
                                        Usz map_width, Usz y, Usz x, Isz offs_y,
                                        Isz offs_x);
ORCA_OK_IF_UNUSED
static void mbuffer_poke_relative(Mbuffer map, Usz map_height, Usz map_width,
                                  Usz y, Usz x, Isz offs_y, Isz offs_x,
                                  Mark_flags flags);

// Inline implementation

ORCA_OK_IF_UNUSED
static Mark_flags mbuffer_peek_relative(Mbuffer map, Usz map_height,
                                        Usz map_width, Usz y, Usz x, Isz offs_y,
                                        Isz offs_x) {
  Isz y0 = (Isz)y + offs_y;
  Isz x0 = (Isz)x + offs_x;
  if (y0 >= (Isz)map_height || x0 >= (Isz)map_width || y0 < 0 || x0 < 0)
    return Mark_flag_none;
  return map[(Usz)y0 * map_width + (Usz)x0];
}

ORCA_OK_IF_UNUSED
static void mbuffer_poke_relative(Mbuffer map, Usz map_height, Usz map_width,
                                  Usz y, Usz x, Isz offs_y, Isz offs_x,
                                  Mark_flags flags) {
  Isz y0 = (Isz)y + offs_y;
  Isz x0 = (Isz)x + offs_x;
  if (y0 >= (Isz)map_height || x0 >= (Isz)map_width || y0 < 0 || x0 < 0)
    return;
  map[(Usz)y0 * map_width + (Usz)x0] = (U8)flags;
}

ORCA_OK_IF_UNUSED
static void mbuffer_poke_relative_flags_or(Mbuffer map, Usz map_height,
                                           Usz map_width, Usz y, Usz x,
                                           Isz offs_y, Isz offs_x,
                                           Mark_flags flags) {
  Isz y0 = (Isz)y + offs_y;
  Isz x0 = (Isz)x + offs_x;
  if (y0 >= (Isz)map_height || x0 >= (Isz)map_width || y0 < 0 || x0 < 0)
    return;
  map[(Usz)y0 * map_width + (Usz)x0] |= (U8)flags;
}

ORCA_OK_IF_UNUSED
static void mbuffer_poke_flags_or(Mbuffer map, Usz map_height, Usz map_width,
                                  Usz y, Usz x, Mark_flags flags) {
  (void)map_height;
  map[y * map_width + x] |= (U8)flags;
}

ORCA_OK_IF_UNUSED
static Mark_flags mbuffer_peek(Mbuffer map, Usz map_height, Usz map_width,
                               Usz y, Usz x) {
  (void)map_height;
  return map[y * map_width + x];
}
