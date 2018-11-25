#include "mark.h"

void markmap_init(Markmap* map) {
  map->buffer = NULL;
  map->capacity = 0;
}

void markmap_ensure_capacity(Markmap* map, Usz capacity) {
  if (map->capacity < capacity) {
    map->buffer = realloc(map->buffer, capacity);
    map->capacity = capacity;
  }
}

void markmap_clear(Markmap* map) {
  memset(map->buffer, 0, map->capacity);
}

void markmap_deinit(Markmap* map) {
  free(map->buffer);
}
