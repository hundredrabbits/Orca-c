#include "base.h"
#include "field.h"
#include <unistd.h>

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  Field field;
  field_init_fill(&field, 32, 32, '.');
  field_fill_subrect(&field, 1, 1, field.height - 2, field.width - 2, 'a');
  field_fill_subrect(&field, 2, 2, field.height - 4, field.width - 4, 'b');
  field_fill_subrect(&field, 3, 3, field.height - 6, field.width - 6, '.');
  field_fput(&field, stdout);
  field_deinit(&field);
  return 0;
}
