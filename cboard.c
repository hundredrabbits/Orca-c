#include "cboard.h"
#include "gbuffer.h"
#include <stdio.h>

Cboard_error cboard_copy(Glyph const* gbuffer, Usz field_height,
                         Usz field_width, Usz rect_y, Usz rect_x, Usz rect_h,
                         Usz rect_w) {
  (void)field_height;
  FILE* fp = popen("xclip -i -selection clipboard 2>/dev/null", "w");
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

Cboard_error cboard_paste(Glyph* gbuffer, Usz height, Usz width, Usz y, Usz x,
                          Usz* out_h, Usz* out_w) {
  FILE* fp = popen("xclip -o -selection clipboard 2>/dev/null", "r");
  Usz start_y = y, start_x = x, max_y = y, max_x = x;
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
        if (x > max_x)
          max_x = x;
        if (y > max_y)
          max_y = y;
      }
      x++;
    }
    if (n < sizeof inbuff)
      break;
  }
  int status = pclose(fp);
  *out_h = max_y - start_y + 1;
  *out_w = max_x - start_x + 1;
  return status ? Cboard_error_process_exit_error : Cboard_error_none;
}
