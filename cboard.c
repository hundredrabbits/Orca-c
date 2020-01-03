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
    fwrite(row, sizeof(Glyph), rect_w, fp);
    if (iy + 1 < rect_h)
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
  char inbuff[512];
  for (;;) {
    size_t n = fread(inbuff, 1, sizeof inbuff, fp);
    for (size_t i = 0; i < n; i++) {
      char c = inbuff[i];
      if (c == '\r' || c == '\n') {
        y++;
        x = start_x;
        continue;
      }
      if (c != ' ' && y < height && x < width) {
        Glyph g = is_valid_glyph((Glyph)c) ? (Glyph)c : '.';
        gbuffer_poke(gbuffer, height, width, y, x, g);
      }
      x++;
    }
    if (n < sizeof inbuff)
      break;
  }
  int status = pclose(fp);
  return status ? Cboard_error_process_exit_error : Cboard_error_none;
}
