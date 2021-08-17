#pragma once
#include "state.h"
#include "vmio.h"

void orca_run(Glyph *restrict gbuffer, Mark *restrict mbuffer, Usz height,
              Usz width, Oevent_list *oevent_list, Usz random_seed, State *state);
