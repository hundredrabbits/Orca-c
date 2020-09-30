#pragma once
#include "base.h"
#include "vmio.h"

typedef struct {
  char name[8];
  Usz  x;
  Usz  y;
}Guide;

void orca_run(Glyph *restrict gbuffer, Mark *restrict mbuffer, Usz height,
              Usz width, Usz tick_number, Oevent_list *oevent_list,
              Usz random_seed, Guide *guide);
