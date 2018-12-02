#include "bank.h"
#include "base.h"
#include "field.h"
#include "mark.h"
#include "sim.h"
#include <getopt.h>
#include <locale.h>
#include <ncurses.h>

#define AND_CTRL(c) ((c)&037)

static void usage() {
  // clang-format off
  fprintf(stderr,
      "Usage: ui [options] infile\n\n"
      "Options:\n"
      "    -h or --help  Print this message and exit.\n"
      );
  // clang-format on
}

typedef enum {
  C_natural,
  C_black,
  C_red,
  C_green,
  C_yellow,
  C_blue,
  C_magenta,
  C_cyan,
  C_white,
} Color_name;

enum {
  Colors_count = C_white + 1,
};

enum {
  Cdef_normal = COLOR_PAIR(1),
};

typedef enum {
  A_normal = A_NORMAL,
  A_bold = A_BOLD,
  A_dim = A_DIM,
  A_reverse = A_REVERSE,
} Term_attr;

ORCA_FORCE_INLINE
int fg_bg(Color_name fg, Color_name bg) {
  return COLOR_PAIR(1 + fg * Colors_count + bg);
}

typedef enum {
  Glyph_class_unknown,
  Glyph_class_grid,
  Glyph_class_comment,
  Glyph_class_uppercase,
  Glyph_class_lowercase,
  Glyph_class_movement,
  Glyph_class_numeric,
  Glyph_class_bang,
} Glyph_class;

static Glyph_class glyph_class_of(Glyph glyph) {
  if (glyph == '.' || glyph == '+')
    return Glyph_class_grid;
  if (glyph >= '0' && glyph <= '9')
    return Glyph_class_numeric;
  switch (glyph) {
  case 'N':
  case 'n':
  case 'E':
  case 'e':
  case 'S':
  case 's':
  case 'W':
  case 'w':
  case 'Z':
  case 'z':
    return Glyph_class_movement;
  case '*':
    return Glyph_class_bang;
  case '#':
    return Glyph_class_comment;
  }
  if (glyph >= 'A' && glyph <= 'Z')
    return Glyph_class_uppercase;
  if (glyph >= 'a' && glyph <= 'z')
    return Glyph_class_lowercase;
  return Glyph_class_unknown;
}

static chtype chtype_of_cell(Glyph g, Mark m) {
  Glyph_class gclass = glyph_class_of(g);
  int attr = A_normal;
  switch (gclass) {
  case Glyph_class_unknown:
    attr = A_bold | fg_bg(C_red, C_natural);
    break;
  case Glyph_class_grid:
    attr = A_bold | fg_bg(C_black, C_natural);
    break;
  case Glyph_class_comment:
    attr = A_dim | Cdef_normal;
    break;
  case Glyph_class_uppercase:
    attr = A_normal | fg_bg(C_black, C_cyan);
    break;
  case Glyph_class_lowercase:
  case Glyph_class_movement:
  case Glyph_class_numeric:
    attr = A_bold | Cdef_normal;
    break;
  case Glyph_class_bang:
    attr = A_bold | Cdef_normal;
    break;
  }
  if (gclass != Glyph_class_comment) {
    if ((m & (Mark_flag_lock | Mark_flag_input)) ==
        (Mark_flag_lock | Mark_flag_input)) {
      attr = A_normal | Cdef_normal;
    } else if (m & Mark_flag_lock) {
      attr = A_dim | Cdef_normal;
    }
  }
  if (m & Mark_flag_output) {
    attr = A_reverse;
  }
  if (m & Mark_flag_haste_input) {
    attr = A_bold | fg_bg(C_cyan, C_natural);
  }
  return (chtype)((int)g | attr);
}

void draw_ui_bar(WINDOW* win, int win_y, int win_x, const char* filename,
                 Usz tick_num) {
  wmove(win, win_y, win_x);
  wattrset(win, A_dim | Cdef_normal);
  wprintw(win, "%s    tick ", filename);
  wattrset(win, A_normal | fg_bg(C_white, C_natural));
  wprintw(win, "%d", (int)tick_num);
  // wprintw(win, "   q: quit    space: step ");
  wclrtoeol(win);
}

void draw_debug_field(WINDOW* win, int term_h, int term_w, int pos_y, int pos_x,
                      Glyph const* gbuffer, Mark const* mbuffer, Usz field_h,
                      Usz field_w, Usz ruler_spacing_y, Usz ruler_spacing_x) {
  enum { Bufcount = 4096 };
  (void)term_h;
  (void)term_w;
  if (field_w > Bufcount)
    return;
  chtype buffer[Bufcount];
  bool use_rulers = ruler_spacing_y != 0 && ruler_spacing_x != 0;
  for (Usz y = 0; y < field_h; ++y) {
    Glyph const* gline = gbuffer + y * field_w;
    Mark const* mline = mbuffer + y * field_w;
    bool use_y_ruler = use_rulers && y % ruler_spacing_y == 0;
    for (Usz x = 0; x < field_w; ++x) {
      Glyph g = gline[x];
      Mark m = mline[x];
      if (g == '.') {
        if (use_y_ruler && x % ruler_spacing_x == 0)
          g = '+';
      }
      buffer[x] = chtype_of_cell(g, m);
    }
    wmove(win, pos_y + (int)y, pos_x);
    waddchnstr(win, buffer, (int)field_w);
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
  mbuffer_clear(markmap_r.buffer, field.height, field.width);
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
  // raw();
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

  for (int ifg = 0; ifg < Colors_count; ++ifg) {
    for (int ibg = 0; ibg < Colors_count; ++ibg) {
      int res = init_pair((short int)(1 + ifg * Colors_count + ibg),
                          (short int)(ifg - 1), (short int)(ibg - 1));
      if (res == ERR) {
        endwin();
        fprintf(stderr, "Error initializing color\n");
        exit(1);
      }
    }
  }

  Usz tick_num = 0;
  for (;;) {
    int term_height = getmaxy(stdscr);
    int term_width = getmaxx(stdscr);
    assert(term_height >= 0 && term_width >= 0);
    (void)term_height;
    (void)term_width;
    // clear();
    draw_debug_field(stdscr, term_height, term_width, 0, 0, field.buffer,
                     markmap_r.buffer, field.height, field.width, 8, 8);
    for (int y = field.height; y < term_height - 1; ++y) {
      wmove(stdscr, y, 0);
      wclrtoeol(stdscr);
    }
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
    case AND_CTRL('q'):
    case AND_CTRL('d'):
    case AND_CTRL('g'):
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
