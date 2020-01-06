#include "prefs.h"
#include <ctype.h>

ORCA_FORCE_NO_INLINE
Conf_read_result conf_read_line(FILE* file, char* buf, Usz bufsize,
                                char** out_left, Usz* out_leftsize,
                                char** out_right, Usz* out_rightsize) {
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
  a0 = 0; // start of left
  for (;;) {
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
  a1 = a0; // end of left
  for (;;) {
    a1++;
    if (a1 == len)
      goto ignore;
    char c = s[a1];
    Usz x = a1;
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
  for (;;) {
    b0++;
    if (b0 == len)
      goto ignore;
    char c = s[b0];
    if (!isspace(c))
      break;
  }
  b1 = b0; // end of right
  for (;;) {
    b1++;
    if (b1 == len)
      goto ok;
    char c = s[b1];
    Usz x = b1;
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
