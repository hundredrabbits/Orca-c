#pragma once
#include "bank.h"
#include "base.h"

#define ORCA_PIANO_KEYS_COUNT ((size_t)(('9' - '0') + 1 + ('z' - 'a') + 1))
#define ORCA_PIANO_BITS_NONE UINT64_C(0)
typedef U64 Piano_bits;

static inline Piano_bits piano_bits_of(Glyph g) {
  if (g >= '0' && g <= '9')
    return UINT64_C(1) << (U64)((Usz)('9' - g));
  if (g >= 'a' && g <= 'z')
    return UINT64_C(1) << (U64)(((Usz)('z' - g)) + ((Usz)('9' - '0')) + 1);
  return UINT64_C(0);
}

void orca_run(Glyph* restrict gbuffer, Mark* restrict mbuffer, Usz height,
              Usz width, Usz tick_number, Oevent_list* oevent_list,
              Piano_bits piano_bits, Usz random_seed);
