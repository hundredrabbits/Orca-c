#include "base.h"

typedef struct {
  U32 grid_index;
  U8 size;
} Bank_entry;

typedef struct {
  char* data;
  Usz capacity;
} Bank;

typedef char* Bank_cursor;

void bank_init(Bank* bank);
void bank_deinit(Bank* bank);
