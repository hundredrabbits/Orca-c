#pragma once
#include "base.h"
#include <stdio.h> // FILE cannot be forward declared

typedef enum {
  Cboard_error_none = 0,
  Cboard_error_unavailable,
  Cboard_error_popen_failed,
  Cboard_error_process_exit_error,
} Cboard_error;

Cboard_error cboard_copy(Glyph const* gbuffer, Usz field_height,
                         Usz field_width, Usz rect_y, Usz rect_x, Usz rect_h,
                         Usz rect_w);

Cboard_error cboard_paste(Glyph* gbuffer, Usz height, Usz width, Usz y, Usz x,
                          Usz* out_h, Usz* out_w);

typedef enum {
  Conf_read_left_and_right = 0, // left and right will be set
  Conf_read_irrelevant,         // only left will be set
  Conf_read_buffer_too_small,   // neither will be set
  Conf_read_eof,                // "
  Conf_read_io_error,           // "
} Conf_read_result;

Conf_read_result conf_read_line(FILE* file, char* buf, Usz bufsize,
                                char** out_left, Usz* out_leftlen,
                                char** out_right, Usz* out_rightlen);

FILE* conf_file_open_for_reading(void);

typedef struct {
  FILE *origfile, *tempfile;
  struct oso *canonpath, *temppath;
} Conf_save;

typedef enum {
  Conf_save_start_ok = 0,
  Conf_save_start_alloc_failed,
  Conf_save_start_no_home,
  Conf_save_start_mkdir_failed,
  Conf_save_start_old_temp_file_stuck,
  Conf_save_start_temp_file_open_failed,
} Conf_save_start_error;

typedef enum {
  Conf_save_commit_ok = 0,
  Conf_save_commit_temp_fsync_failed,
  Conf_save_commit_temp_close_failed,
  Conf_save_commit_rename_failed,
} Conf_save_commit_error;

Conf_save_start_error conf_save_start(Conf_save* p);
void conf_save_cancel(Conf_save* p);
Conf_save_commit_error conf_save_commit(Conf_save* p);
