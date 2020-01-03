#include "cboard.h"
#include "gbuffer.h"
#include <stdio.h>

Cboard_error cboard_copy(Glyph const* gbuffer, Usz field_height,
                         Usz field_width, Usz rect_y, Usz rect_x, Usz rect_h,
                         Usz rect_w) {
  (void)field_height;
  FILE* fp = popen("xclip -i -selection clipboard", "w");
  if (!fp)
    return Cboard_error_popen_failed;
  for (Usz iy = 0; iy < rect_h; iy++) {
    Glyph const* row = gbuffer + (rect_y + iy) * field_width + rect_x;
    for (Usz ix = 0; ix < rect_w; ix++) {
      fputc(row[ix], fp);
    }
    if (iy < rect_h + 1)
      fputc('\n', fp);
  }
  int status = pclose(fp);
  return status ? Cboard_error_process_exit_error : Cboard_error_none;
}

Cboard_error cboard_paste(Glyph* gbuffer, Usz height, Usz width, Usz y, Usz x) {
  FILE* fp = popen("xclip -o -selection clipboard", "r");
  Usz start_x = x;
  if (!fp)
    return Cboard_error_popen_failed;
  for (;;) {
    int c = fgetc(fp);
    if (c == EOF)
      break;
    if (c == '\r' || c == '\n') {
      y++;
      x = start_x;
      continue;
    }
    if (c != ' ' && y < height && x < width) {
      Glyph g = c <= CHAR_MAX && c >= CHAR_MIN && is_valid_glyph((Glyph)c)
                    ? (Glyph)c
                    : '.';
      gbuffer_poke(gbuffer, height, width, y, x, g);
    }
    x++;
  }
  int status = pclose(fp);
  return status ? Cboard_error_process_exit_error : Cboard_error_none;
}
