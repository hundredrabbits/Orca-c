#pragma once
#include "base.h"

typedef enum {
  Cboard_error_none = 0,
  Cboard_error_unavailable,
  Cboard_error_popen_failed,
  Cboard_error_process_exit_error,
} Cboard_error;

Cboard_error cboard_copy(Glyph const* gbuffer, Usz field_height,
                         Usz field_width, Usz rect_y, Usz rect_x, Usz rect_h,
                         Usz rect_w);

Cboard_error cboard_paste(Glyph* gbuffer, Usz height, Usz width, Usz y, Usz x);
