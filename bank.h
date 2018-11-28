#pragma once
#include "base.h"

typedef struct {
  U32 index;
  U8 size;
} Bank_entry;

typedef struct {
  char* data;
  Usz capacity;
} Bank;

typedef size_t Bank_cursor;

#define ORCA_BANK_INDEX_MAX UINT32_MAX
#define ORCA_BANK_ENTRY_GLYPHS_MAX UINT8_MAX

void bank_init(Bank* bank);
void bank_deinit(Bank* bank);
void bank_enlarge_to(Bank* bank, Usz bytes);
void bank_reserve_average(Bank* bank, Usz num_entries, Usz avg_glyph_count);
static inline void bank_cursor_reset(Bank_cursor* cursor) { *cursor = 0; }

Usz bank_append(Bank* restrict bank, Usz cur_size, Usz index,
                Glyph* restrict glyphs, Usz glyph_count);
Usz bank_read(char const* bank_data, Usz bank_size,
              Bank_cursor* restrict cursor, Usz index, Usz num_to_read,
              Glyph* restrict dest, Usz dest_count);
