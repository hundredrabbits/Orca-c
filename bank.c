#include "bank.h"

#define ORCA_BANK_ENTRY_HEADER (sizeof(U32) + sizeof(U8))
#define ORCA_BANK_ENTRY_ALIGN sizeof(Bank_entry)

ORCA_FORCE_STATIC_INLINE
Usz bank_entry_padding(Usz glyph_count) {
  return ORCA_BANK_ENTRY_ALIGN -
         (ORCA_BANK_ENTRY_HEADER + glyph_count) % ORCA_BANK_ENTRY_ALIGN;
}
ORCA_FORCE_STATIC_INLINE
Usz bank_entry_size(Usz glyph_count) {
  return ORCA_BANK_ENTRY_HEADER + bank_entry_padding(glyph_count);
}

void bank_init(Bank* bank) {
  bank->data = NULL;
  bank->capacity = 0;
}

void bank_deinit(Bank* bank) { free(bank->data); }

void bank_enlarge_to(Bank* bank, Usz bytes) {
  Usz new_cap = bytes < 256 ? 256 : orca_round_up_power2(bytes);
  bank->data = realloc(bank->data, new_cap);
  bank->capacity = new_cap;
}

void bank_reserve(Bank* bank, Usz entries, Usz avg_count) {
  Usz avg_size = bank_entry_size(avg_count);
  Usz total_bytes = entries * avg_size;
  if (bank->capacity < total_bytes) {
    Usz new_cap = orca_round_up_power2(total_bytes);
    bank->data = realloc(bank->data, new_cap);
  }
}

Usz bank_append(Bank* restrict bank, Usz cur_size, Usz index,
                Glyph* restrict glyphs, Usz glyph_count) {
  assert(index <= ORCA_BANK_INDEX_MAX);
  assert(glyph_count <= ORCA_BANK_ENTRY_GLYPHS_MAX);
  // no overflow check
  Usz new_size = cur_size + bank_entry_size(glyph_count);
  if (new_size > bank->capacity)
    bank_enlarge_to(bank, new_size);
  char* data = bank->data;
  Bank_entry* entry =
      (Bank_entry*)ORCA_ASSUME_ALIGNED(data, ORCA_BANK_ENTRY_ALIGN);
  entry->index = (U32)index;
  entry->size = (U8)glyph_count;
  data += ORCA_BANK_ENTRY_HEADER;
  memcpy(data, glyphs, glyph_count);
#ifndef NDEBUG
  Usz padding = bank_entry_padding(glyph_count);
  memset(data + glyph_count, 0x1c, padding);
#endif
  return new_size;
}

Usz bank_read(char const* bank_data, Usz bank_size,
              Bank_cursor* restrict cursor, Usz index, Glyph* restrict dest,
              Usz dest_count) {
  assert(index <= ORCA_BANK_INDEX_MAX);
  Usz offset = *cursor;
  Bank_entry* entry;
  Usz entry_index;
  Usz entry_size;
  Usz num_to_copy;

next:
  if (offset == bank_size)
    goto fail;
  entry = (Bank_entry*)ORCA_ASSUME_ALIGNED(bank_data + offset,
                                           ORCA_BANK_ENTRY_ALIGN);
  entry_index = entry->index;
  if (entry_index > index)
    goto fail;
  entry_size = entry->size;
  if (entry_index < index) {
    offset += ORCA_BANK_ENTRY_HEADER + entry_size;
    goto next;
  }
  num_to_copy = dest_count < entry_size ? dest_count : entry_size;
  memcpy(dest, bank_data + offset + ORCA_BANK_ENTRY_HEADER, num_to_copy);
  if (num_to_copy < dest_count)
    memset(dest, '.', dest_count - num_to_copy);
  *cursor = ORCA_BANK_ENTRY_HEADER + entry_size;
  return num_to_copy;

fail:
  memset(dest, '.', dest_count);
  *cursor = offset;
  return 0;
}
