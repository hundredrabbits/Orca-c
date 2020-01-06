#pragma once
#include "base.h"
#include <stdio.h> // FILE cannot be forward declared

typedef enum {
  Readprefs_left_and_right = 0,
  Readprefs_irrelevant,
  Readprefs_buffer_too_small,
  Readprefs_eof,
  Readprefs_io_error,
} Readprefs_result;

Readprefs_result prefs_read_line(FILE* file, char* buf, Usz bufsize,
                                 Usz* out_linelen, char** out_left,
                                 Usz* out_leftlen, char** out_right,
                                 Usz* out_rightlen);
