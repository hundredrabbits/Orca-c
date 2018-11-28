#include "base.h"

typedef struct {
  U32 grid_index;
  U8 size;
} Bank_entry;

typedef struct {
  char* data;
  Usz capacity;
} Bank;

typedef size_t Bank_cursor;

#define ORCA_BANK_GRID_INDEX_MAX UINT32_MAX
#define ORCA_BANK_ENTRY_GLYPHS_MAX UINT8_MAX
#define ORCA_BANK_ENTRY_HEADER (sizeof(U32) + sizeof(U8))
#define ORCA_BANK_ENTRY_ALIGN sizeof(Bank_entry)

void bank_init(Bank* bank);
void bank_deinit(Bank* bank);
void bank_enlarge_to(Bank* bank, Usz bytes);
void bank_reserve_average(Bank* bank, Usz num_entries, Usz avg_glyph_count);

static inline Usz bank_append(Bank* restrict bank, Usz cur_size, Usz grid_index,
                              Glyph* restrict glyphs, Usz glyph_count);

ORCA_FORCE_STATIC_INLINE
Usz bank_entry_padding(Usz glyph_count) {
  return ORCA_BANK_ENTRY_ALIGN -
         (ORCA_BANK_ENTRY_HEADER + glyph_count) % ORCA_BANK_ENTRY_ALIGN;
}
ORCA_FORCE_STATIC_INLINE
Usz bank_entry_size(Usz glyph_count) {
  return ORCA_BANK_ENTRY_HEADER + bank_entry_padding(glyph_count);
}

static inline Usz bank_append(Bank* restrict bank, Usz cur_size, Usz grid_index,
                              Glyph* restrict glyphs, Usz glyph_count) {
  assert(grid_index <= ORCA_BANK_GRID_INDEX_MAX);
  assert(glyph_count <= ORCA_BANK_ENTRY_GLYPHS_MAX);
  // no overflow check
  Usz new_size = cur_size + bank_entry_size(glyph_count);
  if (new_size > bank->capacity)
    bank_enlarge_to(bank, new_size);
  char* data = bank->data;
  {
    Bank_entry* entry =
        (Bank_entry*)ORCA_ASSUME_ALIGNED(data, ORCA_BANK_ENTRY_ALIGN);
    entry->grid_index = (U32)grid_index;
    entry->size = (U8)glyph_count;
  }
  data += ORCA_BANK_ENTRY_HEADER;
  memcpy(data, glyphs, glyph_count);
#ifndef NDEBUG
  Usz padding = bank_entry_padding(glyph_count);
  memset(data + glyph_count, 0x1c, padding);
#endif
  return new_size;
}
