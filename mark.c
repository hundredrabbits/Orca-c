#include "mark.h"

void markmap_reusable_init(Markmap_reusable* map) {
  map->buffer = NULL;
  map->capacity = 0;
}

void markmap_reusable_ensure_size(Markmap_reusable* map, Usz height,
                                  Usz width) {
  Usz capacity = height * width;
  if (map->capacity < capacity) {
    map->buffer = realloc(map->buffer, capacity);
    map->capacity = capacity;
  }
}

void markmap_reusable_deinit(Markmap_reusable* map) { free(map->buffer); }

void markmap_clear(Markmap_buffer map, Usz height, Usz width) {
  Usz cleared_size = height * width;
  memset(map, 0, cleared_size);
}
