#include "bank.h"

void bank_init(Bank* bank) {
  bank->data = NULL;
  bank->capacity = 0;
}

void bank_deinit(Bank* bank) { free(bank->data); }

void bank_enlarge_to(Bank* bank, Usz bytes) {
  Usz new_cap = orca_round_up_power2(bytes);
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
