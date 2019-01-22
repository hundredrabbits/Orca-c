#include "mark.h"

void mbuf_reusable_init(Mbuf_reusable* mbr) {
  mbr->buffer = NULL;
  mbr->capacity = 0;
}

void mbuf_reusable_ensure_size(Mbuf_reusable* mbr, Usz height, Usz width) {
  Usz capacity = height * width;
  if (mbr->capacity < capacity) {
    mbr->buffer = realloc(mbr->buffer, capacity);
    mbr->capacity = capacity;
  }
}

void mbuf_reusable_deinit(Mbuf_reusable* mbr) { free(mbr->buffer); }

void mbuffer_clear(Mark* mbuf, Usz height, Usz width) {
  Usz cleared_size = height * width;
  memset(mbuf, 0, cleared_size);
}
