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

typedef struct Mbuf_reusable {
  Mark* buffer;
  Usz capacity;
} Mbuf_reusable;

void mbuf_reusable_init(Mbuf_reusable* mbr);
void mbuf_reusable_ensure_size(Mbuf_reusable* mbr, Usz height, Usz width);
void mbuf_reusable_deinit(Mbuf_reusable* mbr);

void mbuffer_clear(Mark* mbuf, Usz height, Usz width);

ORCA_OK_IF_UNUSED
static Mark_flags mbuffer_peek(Mark* mbuf, Usz height, Usz width, Usz y, Usz x);
ORCA_OK_IF_UNUSED
static Mark_flags mbuffer_peek_relative(Mark* mbuf, Usz height, Usz width,
                                        Usz y, Usz x, Isz offs_y, Isz offs_x);
ORCA_OK_IF_UNUSED
static void mbuffer_poke_flags_or(Mark* mbuf, Usz height, Usz width, Usz y,
                                  Usz x, Mark_flags flags);
ORCA_OK_IF_UNUSED
static void mbuffer_poke_relative_flags_or(Mark* mbuf, Usz height, Usz width,
                                           Usz y, Usz x, Isz offs_y, Isz offs_x,
                                           Mark_flags flags);

// Inline implementation

ORCA_OK_IF_UNUSED
static Mark_flags mbuffer_peek(Mark* mbuf, Usz height, Usz width, Usz y,
                               Usz x) {
  (void)height;
  return mbuf[y * width + x];
}

ORCA_OK_IF_UNUSED
static Mark_flags mbuffer_peek_relative(Mark* mbuf, Usz height, Usz width,
                                        Usz y, Usz x, Isz offs_y, Isz offs_x) {
  Isz y0 = (Isz)y + offs_y;
  Isz x0 = (Isz)x + offs_x;
  if (y0 >= (Isz)height || x0 >= (Isz)width || y0 < 0 || x0 < 0)
    return Mark_flag_none;
  return mbuf[(Usz)y0 * width + (Usz)x0];
}

ORCA_OK_IF_UNUSED
static void mbuffer_poke_flags_or(Mark* mbuf, Usz height, Usz width, Usz y,
                                  Usz x, Mark_flags flags) {
  (void)height;
  mbuf[y * width + x] |= (Mark)flags;
}

ORCA_OK_IF_UNUSED
static void mbuffer_poke_relative_flags_or(Mark* mbuf, Usz height, Usz width,
                                           Usz y, Usz x, Isz offs_y, Isz offs_x,
                                           Mark_flags flags) {
  Isz y0 = (Isz)y + offs_y;
  Isz x0 = (Isz)x + offs_x;
  if (y0 >= (Isz)height || x0 >= (Isz)width || y0 < 0 || x0 < 0)
    return;
  mbuf[(Usz)y0 * width + (Usz)x0] |= (Mark)flags;
}
