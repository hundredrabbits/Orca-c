#pragma once
#include "base.h"

typedef struct {
  U32 index : 28;
  U32 count : 4;
} Bank_entry;

typedef struct {
  char* data;
  Usz capacity;
} Bank;

typedef size_t Bank_cursor;

#define ORCA_BANK_ENTRY_HEADER sizeof(Bank_entry)
#define ORCA_BANK_ENTRY_ALIGN sizeof(Bank_entry)
#define ORCA_BANK_INDEX_MAX ((size_t)UINT32_C(0x0FFFFFFF))
#define ORCA_BANK_ENTRY_VALS_MAX ((size_t)UINT32_C(0xF))

void bank_init(Bank* bank);
void bank_deinit(Bank* bank);
void bank_enlarge_to(Bank* bank, Usz bytes);
void bank_reserve_average(Bank* bank, Usz num_entries, Usz avg_entry_count);
static inline void bank_cursor_reset(Bank_cursor* cursor) { *cursor = 0; }

Usz bank_append(Bank* restrict bank, Usz cur_size, Usz index,
                I32 const* restrict vals, Usz vals_count);
Usz bank_read(char const* restrict bank_data, Usz bank_size,
              Bank_cursor* restrict cursor, Usz index, I32* restrict dest,
              Usz dest_count);
