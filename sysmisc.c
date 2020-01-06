#include "sysmisc.h"
#include "gbuffer.h"
#include <ctype.h>

ORCA_FORCE_NO_INLINE
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

ORCA_FORCE_NO_INLINE
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

ORCA_FORCE_NO_INLINE
Conf_read_result conf_read_line(FILE* file, char* buf, Usz bufsize,
                                char** out_left, Usz* out_leftsize,
                                char** out_right, Usz* out_rightsize) {
  // a0 and a1 are the start and end positions of the left side of an "foo=bar"
  // pair. b0 and b1 are the positions right side. Leading and trailing spaces
  // will be removed.
  Usz len, a0, a1, b0, b1;
  char* s;
  if (bufsize < 2)
    goto insufficient_buffer;
#if SIZE_MAX > INT_MAX
  if (bufsize > (Usz)INT_MAX)
    exit(1); // he boot too big
#endif
  s = fgets(buf, (int)bufsize, file);
  if (!s) {
    if (feof(file))
      goto eof;
    goto ioerror;
  }
  len = strlen(buf);
  if (len == bufsize - 1 && buf[len - 1] != '\n' && !feof(file))
    goto insufficient_buffer;
  a0 = 0;
  for (;;) { // scan for first non-space in "   foo=bar"
    if (a0 == len)
      goto ignore;
    char c = s[a0];
    if (c == ';' || c == '#') // comment line, ignore
      goto ignore;
    if (c == '=') // '=' before any other char, bad
      goto ignore;
    if (!isspace(c))
      break;
    a0++;
  }
  a1 = a0;
  for (;;) { // scan for '='
    a1++;
    if (a1 == len)
      goto ignore;
    char c = s[a1];
    Usz x = a1; // don't include any whitespace preceeding the '='
    while (isspace(c)) {
      x++;
      if (x == len)
        goto ignore;
      c = s[x];
    }
    if (c == '=') {
      b0 = x;
      break;
    }
    a1 = x;
  }
  for (;;) { // scan for first non-whitespace after '='
    b0++;
    if (b0 == len)
      goto ignore;
    char c = s[b0];
    if (!isspace(c))
      break;
  }
  b1 = b0;
  for (;;) { // scan for end of useful stuff for right-side value
    b1++;
    if (b1 == len)
      goto ok;
    char c = s[b1];
    Usz x = b1; // don't include any whitespace preceeding the EOL
    while (isspace(c)) {
      x++;
      if (x == len)
        goto ok;
      c = s[x];
    }
    b1 = x;
  }
  Conf_read_result err;
insufficient_buffer:
  err = Conf_read_buffer_too_small;
  goto fail;
eof:
  err = Conf_read_eof;
  goto fail;
ioerror:
  err = Conf_read_io_error;
  goto fail;
fail:
  *out_left = NULL;
  *out_leftsize = 0;
  goto no_right;
ignore:
  s[len - 1] = '\0';
  *out_left = s;
  *out_leftsize = len;
  err = Conf_read_irrelevant;
  goto no_right;
no_right:
  *out_right = NULL;
  *out_rightsize = 0;
  return err;
ok:
  s[a1] = '\0';
  s[b1] = '\0';
  *out_left = s + a0;
  *out_leftsize = a1 - a0;
  *out_right = s + b0;
  *out_rightsize = b1 - b0;
  return Conf_read_left_and_right;
}
