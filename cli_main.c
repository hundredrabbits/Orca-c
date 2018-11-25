#include "base.h"
#include "field.h"
#include "sim.h"
#include <getopt.h>

int main(int argc, char** argv) {
  static struct option cli_options[] = {{"time", required_argument, 0, 't'},
                                        {NULL, 0, NULL, 0}};

  char* input_file = NULL;
  int ticks = 1;

  for (;;) {
    int c = getopt_long(argc, argv, "t:", cli_options, NULL);
    if (c == -1)
      break;
    switch (c) {
    case 't':
      ticks = atoi(optarg);
      if (ticks == 0 && strcmp(optarg, "0")) {
        fprintf(stderr, "Bad time argument %s\n", optarg);
        return 1;
      }
      break;
    case '?':
      return 1;
    }
  }

  if (optind == argc - 1) {
    input_file = argv[optind];
  }

  if (input_file == NULL) {
    fprintf(stderr, "No input file\n");
    return 1;
  }
  if (ticks < 0) {
    fprintf(stderr, "Time must be >= 0\n");
    return 1;
  }

  Field field;
  field_init(&field);
  Field_load_error fle = field_load_file(input_file, &field);
  if (fle != Field_load_error_ok) {
    field_deinit(&field);
    char const* errstr = "Unknown";
    switch (fle) {
    case Field_load_error_ok:
      break;
    case Field_load_error_cant_open_file:
      errstr = "Unable to open file";
      break;
    case Field_load_error_too_many_columns:
      errstr = "Grid file has too many columns";
      break;
    case Field_load_error_no_rows_read:
      errstr = "Grid file has no rows";
      break;
    case Field_load_error_not_a_rectangle:
      errstr = "Grid file is not a rectangle";
      break;
    }
    fprintf(stderr, "File load error: %s\n", errstr);
    return 1;
  }
  for (int i = 0; i < ticks; ++i) {
    orca_run(&field);
  }
  field_fput(&field, stdout);
  field_deinit(&field);
  return 0;
}
