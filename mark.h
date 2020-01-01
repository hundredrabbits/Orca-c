#pragma once
#include "base.h"

typedef struct Mbuf_reusable {
  Mark* buffer;
  Usz capacity;
} Mbuf_reusable;

void mbuf_reusable_init(Mbuf_reusable* mbr);
void mbuf_reusable_ensure_size(Mbuf_reusable* mbr, Usz height, Usz width);
void mbuf_reusable_deinit(Mbuf_reusable* mbr);
