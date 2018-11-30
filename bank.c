#include "bank.h"

ORCA_FORCE_STATIC_INLINE
Usz bank_entry_size(Usz num_vals) {
  return sizeof(Bank_entry) + sizeof(I32) * num_vals;
}

void bank_init(Bank* bank) {
  bank->data = NULL;
  bank->capacity = 0;
}

void bank_deinit(Bank* bank) { free(bank->data); }

void bank_enlarge_to(Bank* bank, Usz bytes) {
  Usz new_cap = bytes < 512 ? 512 : orca_round_up_power2(bytes);
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
                I32 const* restrict vals, Usz vals_count) {
  assert(index <= ORCA_BANK_INDEX_MAX);
  assert(vals_count <= ORCA_BANK_ENTRY_VALS_MAX);
  // no overflow check
  Usz new_size = cur_size + bank_entry_size(vals_count);
  if (new_size > bank->capacity)
    bank_enlarge_to(bank, new_size);
  char* data = bank->data + cur_size;
  Bank_entry* entry =
      (Bank_entry*)ORCA_ASSUME_ALIGNED(data, ORCA_BANK_ENTRY_ALIGN);
  // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=39170
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
  entry->index = (U32)index;
  entry->count = (U32)vals_count;
#pragma GCC diagnostic pop
  I32* restrict out_vals =
      (I32*)ORCA_ASSUME_ALIGNED(data + ORCA_BANK_ENTRY_HEADER, sizeof(I32));
  for (size_t i = 0; i < vals_count; ++i) {
    out_vals[i] = vals[i];
  }
  // memcpy(data, vals, vals_count * sizeof(I32));
  return new_size;
}

Usz bank_read(char const* restrict bank_data, Usz bank_size,
              Bank_cursor* restrict cursor, Usz index, I32* restrict dest,
              Usz dest_count) {
  assert(index <= ORCA_BANK_INDEX_MAX);
  Usz offset = *cursor;
  Bank_entry* entry;
  Usz entry_index;
  Usz entry_count;
  Usz num_to_copy;

next:
  if (offset == bank_size)
    goto fail;
  entry = (Bank_entry*)ORCA_ASSUME_ALIGNED(bank_data + offset,
                                           ORCA_BANK_ENTRY_ALIGN);
  entry_index = entry->index;
  if (entry_index > index)
    goto fail;
  entry_count = entry->count;
  if (entry_index < index) {
    offset += bank_entry_size(entry_count);
    goto next;
  }
  num_to_copy = dest_count < entry_count ? dest_count : entry_count;
  I32 const* src = (I32 const*)ORCA_ASSUME_ALIGNED(
      bank_data + offset + ORCA_BANK_ENTRY_HEADER, sizeof(I32));
  Usz i = 0;
  for (; i < num_to_copy; ++i) {
    dest[i] = src[i];
  }
  for (; i < dest_count; ++i) {
    dest[i] = 0;
  }
  *cursor = offset + ORCA_BANK_ENTRY_HEADER + entry_count * sizeof(I32);
  return num_to_copy;

fail:
  for (Usz i0 = 0; i0 < dest_count; ++i0) {
    dest[i0] = 0;
  }
  *cursor = offset;
  return 0;
}
