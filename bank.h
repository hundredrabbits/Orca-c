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

ORCA_FORCE_NO_INLINE
Usz bank_append(Bank* restrict bank, Usz cur_size, Usz index,
                I32 const* restrict vals, Usz vals_count);
ORCA_FORCE_NO_INLINE
Usz bank_read(char const* restrict bank_data, Usz bank_size,
              Bank_cursor* restrict cursor, Usz index, I32* restrict dest,
              Usz dest_count);

typedef enum {
  Oevent_type_midi,
} Oevent_types;

typedef struct {
  U8 oevent_type;
  U8 channel;
  U8 octave;
  U8 note;
  U8 velocity;
  U8 bar_divisor;
} Oevent_midi;

typedef union {
  U8 oevent_type;
  Oevent_midi midi;
} Oevent;

typedef struct {
  Oevent* buffer;
  Usz count;
  Usz capacity;
} Oevent_list;

void oevent_list_init(Oevent_list* olist);
void oevent_list_deinit(Oevent_list* olist);
void oevent_list_clear(Oevent_list* olist);
void oevent_list_copy(Oevent_list const* src, Oevent_list* dest);
ORCA_FORCE_NO_INLINE
Oevent* oevent_list_alloc_item(Oevent_list* olist);
