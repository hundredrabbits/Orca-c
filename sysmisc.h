#pragma once
#include "base.h"
#include <stdio.h> // FILE cannot be forward declared

typedef enum {
  Cboard_error_none = 0,
  Cboard_error_unavailable,
  Cboard_error_popen_failed,
  Cboard_error_process_exit_error,
} Cboard_error;

Cboard_error cboard_copy(Glyph const *gbuffer, Usz field_height,
                         Usz field_width, Usz rect_y, Usz rect_x, Usz rect_h,
                         Usz rect_w);

Cboard_error cboard_paste(Glyph *gbuffer, Usz height, Usz width, Usz y, Usz x,
                          Usz *out_h, Usz *out_w);

typedef enum {
  Conf_read_left_and_right = 0, // left and right will be set
  Conf_read_irrelevant,         // only left will be set
  Conf_read_buffer_too_small,   // neither will be set
  Conf_read_eof,                // "
  Conf_read_io_error,           // "
} Conf_read_result;

Conf_read_result conf_read_line(FILE *file, char *buf, Usz bufsize,
                                char **out_left, Usz *out_leftlen,
                                char **out_right, Usz *out_rightlen);

bool conf_read_match(FILE **pfile, char const *const *names, Usz nameslen,
                     char *buf, Usz bufsize, Usz *out_index, char **out_value);

FILE *conf_file_open_for_reading(void);

typedef struct {
  FILE *origfile, *tempfile;
  struct oso *canonpath, *temppath;
} Conf_save;

typedef enum {
  Conf_save_start_ok = 0,
  Conf_save_start_alloc_failed,
  Conf_save_start_no_home,
  Conf_save_start_mkdir_failed,
  Conf_save_start_conf_dir_not_dir,
  Conf_save_start_temp_file_perm_denied,
  Conf_save_start_old_temp_file_stuck,
  Conf_save_start_temp_file_open_failed,
} Conf_save_start_error;

typedef enum {
  Conf_save_commit_ok = 0,
  Conf_save_commit_temp_fsync_failed,
  Conf_save_commit_temp_close_failed,
  Conf_save_commit_rename_failed,
} Conf_save_commit_error;

Conf_save_start_error conf_save_start(Conf_save *p);
// `*p` may be passed in uninitialized or zeroed -- either is fine. If the
// return value is `Conf_save_start_ok`, then you must call either
// `conf_save_cancel()` or `conf_save_commit()`, otherwise file handles and
// strings will be leaked. If the return value is not `Conf_save_start_ok`,
// then the contents of `*p` are zeroed, and nothing further has to be called.
//
// Note that `origfile` in the `struct Conf_save` may be null even if the call
// succeeded and didn't return an error. This is because it's possible for
// there to be no existing config file. It might be the first time a config
// file is being written.

void conf_save_cancel(Conf_save *p);
// Cancels a config save. Closes any file handles and frees any necessary
// strings. Calling with a zeroed `*p` is fine, but don't call it with
// uninitialized data. Afterwards, `*p` will be zeroed.

Conf_save_commit_error conf_save_commit(Conf_save *p);
// Finishes. Do not call this with a zeroed `*p`. Afterwards, `*p` will be
// zeroed.

typedef enum {
  Prefs_save_ok = 0,
  Prefs_save_oom,
  Prefs_save_no_home,
  Prefs_save_mkdir_failed,
  Prefs_save_conf_dir_not_dir,
  Prefs_save_old_temp_file_stuck,
  Prefs_save_temp_file_perm_denied,
  Prefs_save_temp_open_failed,
  Prefs_save_temp_fsync_failed,
  Prefs_save_temp_close_failed,
  Prefs_save_rename_failed,
  Prefs_save_line_too_long,
  Prefs_save_existing_read_error,
  Prefs_save_unknown_error,
} Prefs_save_error;

char const *prefs_save_error_string(Prefs_save_error error);

// Just playing around with this design
typedef struct {
  FILE *file;
  Usz index;
  char *value;
  char buffer[1024];
} Ezconf_read;

void ezconf_read_start(Ezconf_read *ezcr);
bool ezconf_read_step(Ezconf_read *ezcr, char const *const *names,
                      Usz nameslen);
