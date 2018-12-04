#include "bank.h"
#include "base.h"
#include "field.h"
#include "gbuffer.h"
#include "mark.h"
#include "sim.h"
#include <getopt.h>
#include <locale.h>
#include <ncurses.h>

#define AND_CTRL(c) ((c)&037)

static void usage() {
  // clang-format off
  fprintf(stderr,
      "Usage: tui [options] [file]\n\n"
      "Options:\n"
      "    --margins <number> Add cosmetic margins.\n"
      "                       Default: 2\n"
      "    -h or --help       Print this message and exit.\n"
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
  A_standout = A_STANDOUT,
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
  if (glyph == '.')
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

static int term_attrs_of_cell(Glyph g, Mark m) {
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
  return attr;
}

typedef struct {
  Usz y;
  Usz x;
} Tui_cursor;

void tui_cursor_init(Tui_cursor* tc) {
  tc->y = 0;
  tc->x = 0;
}

void tui_cursor_move_relative(Tui_cursor* tc, Usz field_h, Usz field_w,
                              Isz delta_y, Isz delta_x) {
  Isz y0 = (Isz)tc->y + delta_y;
  Isz x0 = (Isz)tc->x + delta_x;
  if (y0 >= (Isz)field_h)
    y0 = (Isz)field_h - 1;
  if (y0 < 0)
    y0 = 0;
  if (x0 >= (Isz)field_w)
    x0 = (Isz)field_w - 1;
  if (x0 < 0)
    x0 = 0;
  tc->y = (Usz)y0;
  tc->x = (Usz)x0;
}

void tdraw_tui_cursor(WINDOW* win, Glyph const* gbuffer, Usz field_h,
                      Usz field_w, Usz ruler_spacing_y, Usz ruler_spacing_x,
                      Usz cursor_y, Usz cursor_x) {
  (void)ruler_spacing_y;
  (void)ruler_spacing_x;
  if (cursor_y >= field_h || cursor_x >= field_w)
    return;
  Glyph beneath = gbuffer[cursor_y * field_w + cursor_x];
  char displayed = beneath == '.' ? '@' : beneath;
  chtype ch =
      (chtype)(displayed | (A_reverse | A_bold | fg_bg(C_yellow, C_natural)));
  wmove(win, (int)cursor_y, (int)cursor_x);
  waddchnstr(win, &ch, 1);
}

typedef struct Undo_node {
  Field field;
  Usz tick_num;
  struct Undo_node* prev;
  struct Undo_node* next;
} Undo_node;

typedef struct {
  Undo_node* first;
  Undo_node* last;
  Usz count;
} Undo_history;

void undo_history_init(Undo_history* hist) {
  hist->first = NULL;
  hist->last = NULL;
  hist->count = 0;
}
void undo_history_deinit(Undo_history* hist) {
  Undo_node* a = hist->first;
  while (a) {
    Undo_node* b = a->next;
    field_deinit(&a->field);
    free(a);
    a = b;
  }
}

enum { Undo_history_max = 500 };

void undo_history_push(Undo_history* hist, Field* field, Usz tick_num) {
  Undo_node* new_node;
  if (hist->count == Undo_history_max) {
    new_node = hist->first;
    if (new_node == hist->last) {
      hist->first = NULL;
      hist->last = NULL;
    } else {
      hist->first = new_node->next;
      hist->first->prev = NULL;
    }
  } else {
    new_node = malloc(sizeof(Undo_node));
    ++hist->count;
    field_init(&new_node->field);
  }
  field_copy(field, &new_node->field);
  new_node->tick_num = tick_num;
  if (hist->last) {
    hist->last->next = new_node;
    new_node->prev = hist->last;
  } else {
    hist->first = new_node;
    hist->last = new_node;
    new_node->prev = NULL;
  }
  new_node->next = NULL;
  hist->last = new_node;
}

void undo_history_pop(Undo_history* hist, Field* out_field, Usz* out_tick_num) {
  Undo_node* last = hist->last;
  if (!last)
    return;
  field_copy(&last->field, out_field);
  *out_tick_num = last->tick_num;
  if (hist->first == last) {
    hist->first = NULL;
    hist->last = NULL;
  } else {
    Undo_node* new_last = last->prev;
    new_last->next = NULL;
    hist->last = new_last;
  }
  field_deinit(&last->field);
  free(last);
  --hist->count;
}

Usz undo_history_count(Undo_history* hist) { return hist->count; }

void tdraw_hud(WINDOW* win, int win_y, int win_x, int height, int width,
               const char* filename, Usz field_h, Usz field_w,
               Usz ruler_spacing_y, Usz ruler_spacing_x, Usz tick_num,
               Tui_cursor* const tui_cursor) {
  (void)height;
  (void)width;
  wmove(win, win_y, win_x);
  wprintw(win, "%dx%d\t%d/%d\t%df\t120\t-------", (int)field_w, (int)field_h,
          (int)ruler_spacing_x, (int)ruler_spacing_y, (int)tick_num);
  wclrtoeol(win);
  wmove(win, win_y + 1, win_x);
  wprintw(win, "%d,%d\t1:1\tcell\t%s", (int)tui_cursor->x, (int)tui_cursor->y,
          filename);
  // wattrset(win, A_dim | Cdef_normal);
  // wprintw(win, "%s ", filename);
  // wattrset(win, A_normal | Cdef_normal);
  wclrtoeol(win);
}

void tdraw_field(WINDOW* win, int term_h, int term_w, int pos_y, int pos_x,
                 Glyph const* gbuffer, Mark const* mbuffer, Usz field_h,
                 Usz field_w, Usz ruler_spacing_y, Usz ruler_spacing_x) {
  enum { Bufcount = 4096 };
  (void)term_h;
  (void)term_w;
  if (field_w > Bufcount)
    return;
  if (pos_y >= term_h || pos_x >= term_w)
    return;
  Usz num_y = (Usz)term_h - (Usz)pos_y;
  Usz num_x = (Usz)term_w - (Usz)pos_x;
  if (field_h < num_y)
    num_y = field_h;
  if (field_w < num_x)
    num_x = field_w;
  chtype buffer[Bufcount];
  bool use_rulers = ruler_spacing_y != 0 && ruler_spacing_x != 0;
  for (Usz y = 0; y < num_y; ++y) {
    Glyph const* gline = gbuffer + y * field_w;
    Mark const* mline = mbuffer + y * field_w;
    bool use_y_ruler = use_rulers && y % ruler_spacing_y == 0;
    for (Usz x = 0; x < num_x; ++x) {
      Glyph g = gline[x];
      Mark m = mline[x];
      int attrs = term_attrs_of_cell(g, m);
      if (g == '.') {
        if (use_y_ruler && x % ruler_spacing_x == 0)
          g = '+';
      }
      buffer[x] = (chtype)((int)g | attrs);
    }
    wmove(win, pos_y + (int)y, pos_x);
    waddchnstr(win, buffer, (int)num_x);
    // Trying to clear to eol with 0 chars remaining on line will clear whole
    // line from start
    if (pos_x + (int)num_x != term_w) {
      wmove(win, pos_y + (int)y, pos_x + (int)num_x);
      wclrtoeol(win);
    }
  }
}

void tui_cursor_confine(Tui_cursor* tc, Usz height, Usz width) {
  if (height == 0 || width == 0)
    return;
  if (tc->y >= height)
    tc->y = height - 1;
  if (tc->x >= width)
    tc->x = width - 1;
}

void tui_resize_grid(Field* field, Markmap_reusable* markmap, Isz delta_h,
                     Isz delta_w, Usz tick_num, Field* scratch_field,
                     Undo_history* undo_hist, Tui_cursor* tui_cursor,
                     bool* needs_remarking) {
  Isz new_height = (Isz)field->height + delta_h;
  Isz new_width = (Isz)field->width + delta_w;
  if (new_height < 1 || new_width < 1)
    return;
  undo_history_push(undo_hist, field, tick_num);
  field_copy(field, scratch_field);
  field_resize_raw(field, (Usz)new_height, (Usz)new_width);
  // junky copies until i write a smarter thing
  memset(field->buffer, '.', (Usz)new_height * (Usz)new_width * sizeof(Glyph));
  gbuffer_copy_subrect(scratch_field->buffer, field->buffer,
                       scratch_field->height, scratch_field->width,
                       field->height, field->width, 0, 0, 0, 0,
                       scratch_field->height, scratch_field->width);
  tui_cursor_confine(tui_cursor, (Usz)new_height, (Usz)new_width);
  markmap_reusable_ensure_size(markmap, (Usz)new_height, (Usz)new_width);
  *needs_remarking = true;
}

enum { Argopt_margins = UCHAR_MAX + 1 };

int main(int argc, char** argv) {
  static struct option tui_options[] = {
      {"margins", required_argument, 0, Argopt_margins},
      {"help", no_argument, 0, 'h'},
      {NULL, 0, NULL, 0}};
  char* input_file = NULL;
  int margin_thickness = 2;
  for (;;) {
    int c = getopt_long(argc, argv, "h", tui_options, NULL);
    if (c == -1)
      break;
    switch (c) {
    case 'h':
      usage();
      return 0;
    case Argopt_margins:
      margin_thickness = atoi(optarg);
      if (margin_thickness == 0 && strcmp(optarg, "0")) {
        fprintf(stderr,
                "Bad margins argument %s.\n"
                "Must be 0 or positive integer.\n",
                optarg);
        return 1;
      }
      break;
    case '?':
      usage();
      return 1;
    }
  }

  if (margin_thickness < 0) {
    fprintf(stderr, "Margins must be >= 0.\n");
    usage();
    return 1;
  }

  if (optind == argc - 1) {
    input_file = argv[optind];
  } else if (optind < argc - 1) {
    fprintf(stderr, "Expected only 1 file argument.\n");
    return 1;
  }

  Field field;
  if (input_file) {
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
  } else {
    input_file = "unnamed";
    field_init_fill(&field, 25, 57, '.');
  }
  Markmap_reusable markmap_r;
  markmap_reusable_init(&markmap_r);
  markmap_reusable_ensure_size(&markmap_r, field.height, field.width);
  mbuffer_clear(markmap_r.buffer, field.height, field.width);
  Bank bank;
  bank_init(&bank);
  Undo_history undo_hist;
  undo_history_init(&undo_hist);

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

  WINDOW* cont_win = NULL;
  int cont_win_h = 0;
  int cont_win_w = 0;

  Field scratch_field;
  field_init(&scratch_field);

  Tui_cursor tui_cursor;
  tui_cursor_init(&tui_cursor);
  Usz tick_num = 0;
  Usz ruler_spacing_y = 8;
  Usz ruler_spacing_x = 8;
  bool needs_remarking = true;
  for (;;) {
    int term_height = getmaxy(stdscr);
    int term_width = getmaxx(stdscr);
    assert(term_height >= 0 && term_width >= 0);
    // We can predictavely step the next simulation tick and then use the
    // resulting markmap buffer for better UI visualization. If we don't do
    // this, after loading a fresh file or after the user performs some edit
    // (or even after a regular simulation step), the new glyph buffer won't
    // have had phase 0 of the simulation run, which means the ports and other
    // flags won't be set on the markmap buffer, so the colors for disabled
    // cells, ports, etc. won't be set.
    //
    // We can just perform a simulation step using the current state, keep the
    // markmap buffer that it produces, then roll back the glyph buffer to
    // where it was before. This should produce results similar to having
    // specialized UI code that looks at each glyph and figures out the ports,
    // etc.
    if (needs_remarking) {
      field_resize_raw_if_necessary(&scratch_field, field.height, field.width);
      field_copy(&field, &scratch_field);
      orca_run(field.buffer, markmap_r.buffer, field.height, field.width,
               tick_num, &bank);
      field_copy(&scratch_field, &field);
      needs_remarking = false;
    }
    int content_y = 0;
    int content_x = 0;
    int content_h = term_height;
    int content_w = term_width;
    int margins_2 = margin_thickness * 2;
    if (margin_thickness > 0 && term_height > margins_2 &&
        term_width > margins_2) {
      content_y += margin_thickness;
      content_x += margin_thickness;
      content_h -= margins_2;
      content_w -= margins_2;
    }
    if (cont_win == NULL || cont_win_h != content_h ||
        cont_win_w != content_w) {
      if (cont_win) {
        delwin(cont_win);
      }
      wclear(stdscr);
      cont_win = derwin(stdscr, content_h, content_w, content_y, content_x);
      cont_win_h = content_h;
      cont_win_w = content_w;
    }
    tdraw_field(cont_win, content_h, content_w, 0, 0, field.buffer,
                markmap_r.buffer, field.height, field.width, ruler_spacing_y,
                ruler_spacing_x);
    for (int y = field.height; y < content_h - 1; ++y) {
      wmove(cont_win, y, 0);
      wclrtoeol(cont_win);
    }
    tdraw_tui_cursor(cont_win, field.buffer, field.height, field.width,
                     ruler_spacing_y, ruler_spacing_x, tui_cursor.y,
                     tui_cursor.x);
    if (content_h > 3) {
      tdraw_hud(cont_win, content_h - 2, 0, 2, content_w, input_file,
                field.height, field.width, ruler_spacing_y, ruler_spacing_x,
                tick_num, &tui_cursor);
    }
    wrefresh(cont_win);

    int key;
    // ncurses gives us ERR if there was no user input. We'll sleep for 0
    // seconds, so that we'll yield CPU time to the OS instead of looping as
    // fast as possible. This avoids battery drain/excessive CPU usage. There
    // are better ways to do this that waste less CPU, but they require doing a
    // little more work on each individual platform (Linux, Mac, etc.)
    for (;;) {
      key = wgetch(stdscr);
      if (key != ERR)
        break;
      sleep(0);
    }

    switch (key) {
    case AND_CTRL('q'):
    case AND_CTRL('d'):
    case AND_CTRL('g'):
      goto quit;
    case KEY_UP:
    case AND_CTRL('k'):
      tui_cursor_move_relative(&tui_cursor, field.height, field.width, -1, 0);
      break;
    case AND_CTRL('j'):
    case KEY_DOWN:
      tui_cursor_move_relative(&tui_cursor, field.height, field.width, 1, 0);
      break;
    case KEY_BACKSPACE:
    case AND_CTRL('h'):
    case KEY_LEFT:
      tui_cursor_move_relative(&tui_cursor, field.height, field.width, 0, -1);
      break;
    case AND_CTRL('l'):
    case KEY_RIGHT:
      tui_cursor_move_relative(&tui_cursor, field.height, field.width, 0, 1);
      break;
    case AND_CTRL('u'):
      if (undo_history_count(&undo_hist) > 0) {
        undo_history_pop(&undo_hist, &field, &tick_num);
        needs_remarking = true;
      }
      break;
    case '[':
      if (ruler_spacing_x > 4)
        --ruler_spacing_x;
      break;
    case ']':
      if (ruler_spacing_x < 16)
        ++ruler_spacing_x;
      break;
    case '{':
      if (ruler_spacing_y > 4)
        --ruler_spacing_y;
      break;
    case '}':
      if (ruler_spacing_y < 16)
        ++ruler_spacing_y;
      break;
    case '(':
      tui_resize_grid(&field, &markmap_r, 0, -1, tick_num, &scratch_field,
                      &undo_hist, &tui_cursor, &needs_remarking);
      break;
    case ')':
      tui_resize_grid(&field, &markmap_r, 0, 1, tick_num, &scratch_field,
                      &undo_hist, &tui_cursor, &needs_remarking);
      break;
    case '_':
      tui_resize_grid(&field, &markmap_r, -1, 0, tick_num, &scratch_field,
                      &undo_hist, &tui_cursor, &needs_remarking);
      break;
    case '+':
      tui_resize_grid(&field, &markmap_r, 1, 0, tick_num, &scratch_field,
                      &undo_hist, &tui_cursor, &needs_remarking);
      break;
    case ' ':
      undo_history_push(&undo_hist, &field, tick_num);
      orca_run(field.buffer, markmap_r.buffer, field.height, field.width,
               tick_num, &bank);
      ++tick_num;
      needs_remarking = true;
      break;
    default:
      if (key >= '!' && key <= '~') {
        undo_history_push(&undo_hist, &field, tick_num);
        gbuffer_poke(field.buffer, field.height, field.width, tui_cursor.y,
                     tui_cursor.x, (char)key);
        // Indicate we want the next simulation step to be run predictavely, so
        // that we can use the reulsting mark buffer for UI visualization. This
        // is "expensive", so it could be skipped for non-interactive input in
        // situations where max throughput is necessary.
        needs_remarking = true;
      }
#if 0
      else {
        fprintf(stderr, "Unknown key number: %d\n", key);
      }
#endif
      break;
    }

    // ncurses gives us the special value KEY_RESIZE if the user didn't
    // actually type anything, but the terminal resized.
    // bool ignored_input = ch < CHAR_MIN || ch > CHAR_MAX || ch == KEY_RESIZE;
  }
quit:
  if (cont_win) {
    delwin(cont_win);
  }
  endwin();
  markmap_reusable_deinit(&markmap_r);
  bank_deinit(&bank);
  field_deinit(&field);
  field_deinit(&scratch_field);
  undo_history_deinit(&undo_hist);
  return 0;
}
