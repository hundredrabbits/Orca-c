#include "sysmisc.h"
#include "gbuffer.h"
#include "oso.h"
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>

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

typedef enum {
  Conf_dir_ok = 0,
  Conf_dir_no_home,
} Conf_dir_error;

static char const* const xdg_config_home_env = "XDG_CONFIG_HOME";
static char const* const home_env = "HOME";
static char const* const conf_file_name = "/orca.conf";

static Conf_dir_error try_get_conf_dir(oso** out) {
  char const* xdgcfgdir = getenv(xdg_config_home_env);
  if (xdgcfgdir) {
    Usz xdgcfgdirlen = strlen(xdgcfgdir);
    if (xdgcfgdirlen > 0) {
      osoputlen(out, xdgcfgdir, xdgcfgdirlen);
      return Conf_dir_ok;
    }
  }
  char const* homedir = getenv(home_env);
  if (homedir) {
    Usz homedirlen = strlen(homedir);
    if (homedirlen > 0) {
      osoputprintf(out, "%s/.config", homedir);
      return Conf_dir_ok;
    }
  }
  return Conf_dir_no_home;
}

FILE* conf_file_open_for_reading(void) {
  oso* path = NULL;
  if (try_get_conf_dir(&path))
    return NULL;
  osocat(&path, conf_file_name);
  if (!path)
    return NULL;
  FILE* file = fopen(osoc(path), "r");
  osofree(path);
  return file;
}

Conf_save_start_error conf_save_start(Conf_save* p) {
  memset(p, 0, sizeof(Conf_save));
  oso* dir = NULL;
  Conf_save_start_error err;
  if (try_get_conf_dir(&dir)) {
    err = Conf_save_start_no_home;
    goto cleanup;
  }
  if (!dir) {
    err = Conf_save_start_alloc_failed;
    goto cleanup;
  }
  osoputoso(&p->canonpath, dir);
  osocat(&p->canonpath, conf_file_name);
  if (!p->canonpath) {
    err = Conf_save_start_alloc_failed;
    goto cleanup;
  }
  osoputoso(&p->temppath, p->canonpath);
  osocat(&p->temppath, ".tmp");
  if (!p->temppath) {
    err = Conf_save_start_alloc_failed;
    goto cleanup;
  }
  // Remove old temp file if it exists. If it exists and we can't remove it,
  // error.
  if (unlink(osoc(p->temppath)) == -1 && errno != ENOENT) {
    switch (errno) {
    case ENOTDIR:
      err = Conf_save_start_conf_dir_not_dir;
      break;
    case EACCES:
      err = Conf_save_start_temp_file_perm_denied;
      break;
    default:
      err = Conf_save_start_old_temp_file_stuck;
      break;
    }
    goto cleanup;
  }
  p->tempfile = fopen(osoc(p->temppath), "w");
  if (!p->tempfile) {
    // Try to create config dir, in case it doesn't exist. (XDG says we should
    // do this, and use mode 0700.)
    mkdir(osoc(dir), 0700);
    p->tempfile = fopen(osoc(p->temppath), "w");
  }
  if (!p->tempfile) {
    err = Conf_save_start_temp_file_open_failed;
    goto cleanup;
  }
  // This may be left as NULL.
  p->origfile = fopen(osoc(p->canonpath), "r");
  // We did it, boys.
  osofree(dir);
  return Conf_save_start_ok;

cleanup:
  osofree(dir);
  conf_save_cancel(p);
  return err;
}

void conf_save_cancel(Conf_save* p) {
  osofree(p->canonpath);
  osofree(p->temppath);
  if (p->origfile)
    fclose(p->origfile);
  if (p->tempfile)
    fclose(p->tempfile);
  memset(p, 0, sizeof(Conf_save));
}

Conf_save_commit_error conf_save_commit(Conf_save* p) {
  Conf_save_commit_error err;
  fclose(p->tempfile);
  p->tempfile = NULL;
  if (p->origfile) {
    fclose(p->origfile);
    p->origfile = NULL;
  }
  // This isn't really atomic. But if we want to close and move a file
  // simultaneously, I think we have to use OS-specific facilities. So I guess
  // this is the best we can do for now. I could be wrong, though. But I
  // couldn't find any good information about it.
  if (rename(osoc(p->temppath), osoc(p->canonpath)) == -1) {
    err = Conf_save_commit_rename_failed;
    goto cleanup;
  }
  err = Conf_save_commit_ok;
cleanup:
  conf_save_cancel(p);
  return err;
}
