#include "bank.h"
#include "base.h"
#include "field.h"
#include "mark.h"
#include "sim.h"
#include <getopt.h>
#include <locale.h>
#include <ncurses.h>

static void usage() {
  // clang-format off
  fprintf(stderr,
      "Usage: ui [options] infile\n\n"
      "Options:\n"
      "    -h or --help  Print this message and exit.\n"
      );
  // clang-format on
}

enum {
  Cpair_default = 1,
  Cpair_grey = 2,

  Tattr_default_bold = A_BOLD | COLOR_PAIR(Cpair_default),
  Tattr_boring_glyph = A_DIM | COLOR_PAIR(Cpair_default),
};

void draw_ui_bar(WINDOW* win, int win_y, int win_x, const char* filename,
                 Usz tick_num) {
  wmove(win, win_y, win_x);
  wattrset(win, A_DIM | COLOR_PAIR(Cpair_default));
  wprintw(win, "%s    tick ", filename);
  wattrset(win, A_NORMAL);
  wprintw(win, "%d", (int)tick_num);
  // wprintw(win, "   q: quit    space: step ");
  wclrtoeol(win);
}

void draw_debug_field(WINDOW* win, int win_y, int win_x, Glyph const* gbuffer,
                      Usz height, Usz width) {
  enum { Bufcount = 4096 };
  if (width > Bufcount)
    return;
  chtype buffer[Bufcount];
  for (Usz y = 0; y < height; ++y) {
    Glyph const* gline = gbuffer + y * width;
    for (Usz x = 0; x < width; ++x) {
      Glyph g = gline[x];
      int attr = g == '.' ? Tattr_boring_glyph : Tattr_default_bold;
      buffer[x] = (chtype)(gline[x] | attr);
    }
    wmove(win, win_y + (int)y, (int)win_x);
    waddchnstr(win, buffer, (int)width);
  }
}

int main(int argc, char** argv) {
  static struct option tui_options[] = {{"help", no_argument, 0, 'h'},
                                        {NULL, 0, NULL, 0}};
  char* input_file = NULL;
  for (;;) {
    int c = getopt_long(argc, argv, "h", tui_options, NULL);
    if (c == -1)
      break;
    switch (c) {
    case 'h':
      usage();
      return 1;
    case '?':
      usage();
      return 1;
    }
  }

  if (optind == argc - 1) {
    input_file = argv[optind];
  } else if (optind < argc - 1) {
    fprintf(stderr, "Expected only 1 file argument.\n");
    return 1;
  }
  if (input_file == NULL) {
    fprintf(stderr, "No input file.\n");
    usage();
    return 1;
  }

  Field field;
  field_init(&field);
  Field_load_error fle = field_load_file(input_file, &field);
  if (fle != Field_load_error_ok) {
    field_deinit(&field);
    char const* errstr = "Unknown";
    switch (fle) {
    case Field_load_error_ok:
      break;
    case Field_load_error_cant_open_file:
      errstr = "Unable to open file";
      break;
    case Field_load_error_too_many_columns:
      errstr = "Grid file has too many columns";
      break;
    case Field_load_error_too_many_rows:
      errstr = "Grid file has too many rows";
      break;
    case Field_load_error_no_rows_read:
      errstr = "Grid file has no rows";
      break;
    case Field_load_error_not_a_rectangle:
      errstr = "Grid file is not a rectangle";
      break;
    }
    fprintf(stderr, "File load error: %s.\n", errstr);
    return 1;
  }
  Markmap_reusable markmap_r;
  markmap_reusable_init(&markmap_r);
  markmap_reusable_ensure_size(&markmap_r, field.height, field.width);
  Bank bank;
  bank_init(&bank);

  // Enable UTF-8 by explicitly initializing our locale before initializing
  // ncurses.
  setlocale(LC_ALL, "");
  // Initialize ncurses
  initscr();
  // Allow ncurses to control newline translation. Fine to use with any modern
  // terminal, and will let ncurses run faster.
  nonl();
  // Set interrupt keys (interrupt, break, quit...) to not flush. Helps keep
  // ncurses state consistent, at the cost of less responsive terminal
  // interrupt. (This will rarely happen.)
  intrflush(stdscr, FALSE);
  // Receive keyboard input immediately, and receive shift, control, etc. as
  // separate events, instead of combined with individual characters.
  raw();
  // Don't echo keyboard input
  noecho();
  // Also receive arrow keys, etc.
  keypad(stdscr, TRUE);
  // Hide the terminal cursor
  curs_set(0);
  // Don't block on calls like getch() -- have it ERR immediately if the user
  // hasn't typed anything. That way we can mix other timers in our code,
  // instead of being a slave only to terminal input.
  // nodelay(stdscr, TRUE);
  // Enable color
  start_color();
  use_default_colors();

  init_pair(Cpair_default, -1, -1);
  init_pair(Cpair_grey, COLOR_WHITE, -1);
  //init_pair(Cpair_gray_default, COLOR_GREY, -1);

  Usz tick_num = 0;
  for (;;) {
    int term_height = getmaxy(stdscr);
    int term_width = getmaxx(stdscr);
    assert(term_height >= 0 && term_width >= 0);
    // clear();
    draw_debug_field(stdscr, 0, 0, field.buffer, field.height, field.width);
    draw_ui_bar(stdscr, term_height - 1, 0, input_file, tick_num);
    //refresh();

    int key;
    // ncurses gives us ERR if there was no user input. We'll sleep for 0
    // seconds, so that we'll yield CPU time to the OS instead of looping as
    // fast as possible. This avoids battery drain/excessive CPU usage. There
    // are better ways to do this that waste less CPU, but they require doing a
    // little more work on each individual platform (Linux, Mac, etc.)
    for (;;) {
      key = getch();
      if (key != ERR)
        break;
      sleep(0);
    }

    switch (key) {
    case 'q':
      goto quit;
    case ' ':
      orca_run(field.buffer, markmap_r.buffer, field.height, field.width,
               tick_num, &bank);
      ++tick_num;
      break;
    }

    // ncurses gives us the special value KEY_RESIZE if the user didn't
    // actually type anything, but the terminal resized.
    // bool ignored_input = ch < CHAR_MIN || ch > CHAR_MAX || ch == KEY_RESIZE;
  }
quit:
  endwin();
  markmap_reusable_deinit(&markmap_r);
  bank_deinit(&bank);
  field_deinit(&field);
  return 0;
}
