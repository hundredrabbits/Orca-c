#pragma once
#include "bank.h"
#include "base.h"

void orca_run(Glyph* restrict gbuffer, Mark* restrict mbuffer, Usz height,
              Usz width, Usz tick_number, Oevent_list* oevent_list,
              Usz random_seed);
