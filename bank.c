#include "bank.h"

void bank_init(Bank* bank) {
  bank->data = NULL;
  bank->capacity = 0;
}

void bank_deinit(Bank* bank) {
  free(bank->data);
}
