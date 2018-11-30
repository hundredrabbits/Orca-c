#pragma once
#include "bank.h"
#include "base.h"
#include "mark.h"

void orca_run(Gbuffer gbuf, Mbuffer markmap, Usz height, Usz width,
              Usz tick_number, Bank* bank);
