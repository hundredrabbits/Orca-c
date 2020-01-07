#include "bank.h"
#include "base.h"
#include "field.h"
#include "gbuffer.h"
#include "osc_out.h"
#include "sdd.h"
#include "sim.h"
#include "sysmisc.h"
#include "term_util.h"
#include <getopt.h>
#include <locale.h>

#define SOKOL_IMPL
#include "sokol_time.h"
#undef SOKOL_IMPL

#ifdef FEAT_PORTMIDI
#include <portmidi.h>
#endif

#define TIME_DEBUG 0
#if TIME_DEBUG
static int spin_track_timeout = 0;
#endif

static void usage(void) {
  // clang-format off
  fprintf(stderr,
"Usage: orca [options] [file]\n\n"
"General options:\n"
"    --margins <nxn>        Set cosmetic margins.\n"
"                           Default: 2x1\n"
"    --undo-limit <number>  Set the maximum number of undo steps.\n"
"                           If you plan to work with large files,\n"
"                           set this to a low number.\n"
"                           Default: 100\n"
"    --initial-size <nxn>   When creating a new grid file, use these\n"
"                           starting dimensions.\n"
"    --bpm <number>         Set the tempo (beats per minute).\n"
"                           Default: 120\n"
"    --seed <number>        Set the seed for the random function.\n"
"                           Default: 1\n"
"    -h or --help           Print this message and exit.\n"
"\n"
"OSC/MIDI options:\n"
"    --strict-timing\n"
"        Reduce the timing jitter of outgoing MIDI and OSC messages.\n"
"        Uses more CPU time.\n"
"\n"
"    --osc-server <address>\n"
"        Hostname or IP address to send OSC messages to.\n"
"        Default: loopback (this machine)\n"
"\n"
"    --osc-port <number or service name>\n"
"        UDP port (or service name) to send OSC messages to.\n"
"        This option must be set for OSC output to be enabled.\n"
"        Default: none\n"
"\n"
"    --osc-midi-bidule <path>\n"
"        Set MIDI to be sent via OSC formatted for Plogue Bidule.\n"
"        The path argument is the path of the Plogue OSC MIDI device.\n"
"        Example: /OSC_MIDI_0/MIDI\n"
#ifdef FEAT_PORTMIDI
"\n"
"    --portmidi-list-devices\n"
"        List the MIDI output devices available through PortMidi,\n"
"        along with each associated device ID number, and then exit.\n"
"        Do this to figure out which ID to use with\n"
"        --portmidi-output-device\n"
"\n"
"    --portmidi-output-device <number>\n"
"        Set MIDI to be sent via PortMidi on a specified device ID.\n"
"        Example: 1\n"
#endif
  ); // clang-format on
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
  case '!':
  case ':':
  case ';':
  case '=':
    return Glyph_class_lowercase;
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

static attr_t term_attrs_of_cell(Glyph g, Mark m) {
  Glyph_class gclass = glyph_class_of(g);
  attr_t attr = A_normal;
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
      // Standard locking input
      attr = A_normal | Cdef_normal;
    } else if ((m & Mark_flag_input) == Mark_flag_input) {
      // Non-locking input
      attr = A_normal | Cdef_normal;
    } else if (m & Mark_flag_lock) {
      // Locked only
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

typedef enum {
  Ged_input_mode_normal = 0,
  Ged_input_mode_append,
  Ged_input_mode_selresize,
  Ged_input_mode_slide,
} Ged_input_mode;

typedef struct {
  Usz y;
  Usz x;
  Usz h;
  Usz w;
} Ged_cursor;

void ged_cursor_init(Ged_cursor* tc) {
  tc->y = 0;
  tc->x = 0;
  tc->h = 1;
  tc->w = 1;
}

void ged_cursor_move_relative(Ged_cursor* tc, Usz field_h, Usz field_w,
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

void draw_grid_cursor(WINDOW* win, int draw_y, int draw_x, int draw_h,
                      int draw_w, Glyph const* gbuffer, Usz field_h,
                      Usz field_w, int scroll_y, int scroll_x, Usz cursor_y,
                      Usz cursor_x, Usz cursor_h, Usz cursor_w,
                      Ged_input_mode input_mode, bool is_playing) {
  (void)input_mode;
  if (cursor_y >= field_h || cursor_x >= field_w)
    return;
  if (scroll_y < 0) {
    draw_y += -scroll_y;
    scroll_y = 0;
  }
  if (scroll_x < 0) {
    draw_x += -scroll_x;
    scroll_x = 0;
  }
  Usz offset_y = (Usz)scroll_y;
  Usz offset_x = (Usz)scroll_x;
  if (offset_y >= field_h || offset_x >= field_w)
    return;
  if (draw_y >= draw_h || draw_x >= draw_w)
    return;
  attr_t const curs_attr = A_reverse | A_bold | fg_bg(C_yellow, C_natural);
  if (offset_y <= cursor_y && offset_x <= cursor_x) {
    Usz cdraw_y = cursor_y - offset_y + (Usz)draw_y;
    Usz cdraw_x = cursor_x - offset_x + (Usz)draw_x;
    if (cdraw_y < (Usz)draw_h && cdraw_x < (Usz)draw_w) {
      Glyph beneath = gbuffer[cursor_y * field_w + cursor_x];
      char displayed;
      if (beneath == '.') {
        displayed = is_playing ? '@' : '~';
      } else {
        displayed = beneath;
      }
      chtype ch = (chtype)displayed | curs_attr;
      wmove(win, (int)cdraw_y, (int)cdraw_x);
      waddchnstr(win, &ch, 1);
    }
  }

  // Early out for selection area that won't have any visual effect
  if (cursor_h <= 1 && cursor_w <= 1)
    return;

  // Now mutate visually selected area under grid to have the selection color
  // attributes. (This will rewrite the attributes on the cursor character we
  // wrote above, but if it was the only character that would have been
  // changed, we already early-outed.)
  //
  // We'll do this by reading back the characters on the grid from the curses
  // window buffer, changing the attributes, then writing it back. This is
  // easier than pulling the glyphs from the gbuffer, since we already did the
  // ruler calculations to turn . into +, and we don't need special behavior
  // for any other attributes (e.g. we don't show a special state for selected
  // uppercase characters.)
  //
  // First, confine cursor selection to the grid field/gbuffer that actually
  // exists, in case the cursor selection exceeds the area of the field.
  Usz sel_rows = field_h - cursor_y;
  if (cursor_h < sel_rows)
    sel_rows = cursor_h;
  Usz sel_cols = field_w - cursor_x;
  if (cursor_w < sel_cols)
    sel_cols = cursor_w;
  // Now, confine the selection area to what's visible on screen. Kind of
  // tricky since we have to handle it being partially visible from any edge on
  // any axis, and we have to be mindful overflow.
  Usz vis_sel_y;
  Usz vis_sel_x;
  if (offset_y > cursor_y) {
    vis_sel_y = 0;
    Usz sub_y = offset_y - cursor_y;
    if (sub_y > sel_rows)
      sel_rows = 0;
    else
      sel_rows -= sub_y;
  } else {
    vis_sel_y = cursor_y - offset_y;
  }
  if (offset_x > cursor_x) {
    vis_sel_x = 0;
    Usz sub_x = offset_x - cursor_x;
    if (sub_x > sel_cols)
      sel_cols = 0;
    else
      sel_cols -= sub_x;
  } else {
    vis_sel_x = cursor_x - offset_x;
  }
  vis_sel_y += (Usz)draw_y;
  vis_sel_x += (Usz)draw_x;
  if (vis_sel_y >= (Usz)draw_h || vis_sel_x >= (Usz)draw_w)
    return;
  Usz vis_sel_h = (Usz)draw_h - vis_sel_y;
  Usz vis_sel_w = (Usz)draw_w - vis_sel_x;
  if (sel_rows < vis_sel_h)
    vis_sel_h = sel_rows;
  if (sel_cols < vis_sel_w)
    vis_sel_w = sel_cols;
  if (vis_sel_w == 0 || vis_sel_h == 0)
    return;
  enum { Bufcount = 4096 };
  chtype chbuffer[Bufcount];
  if (Bufcount < vis_sel_w)
    vis_sel_w = Bufcount;
  for (Usz iy = 0; iy < vis_sel_h; ++iy) {
    int at_y = (int)(vis_sel_y + iy);
    int num = mvwinchnstr(win, at_y, (int)vis_sel_x, chbuffer, (int)vis_sel_w);
    for (int ix = 0; ix < num; ++ix) {
      chbuffer[ix] = (chtype)((chbuffer[ix] & (A_CHARTEXT | A_ALTCHARSET)) |
                              (chtype)curs_attr);
    }
    waddchnstr(win, chbuffer, (int)num);
  }
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
  Usz limit;
} Undo_history;

void undo_history_init(Undo_history* hist, Usz limit) {
  hist->first = NULL;
  hist->last = NULL;
  hist->count = 0;
  hist->limit = limit;
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

void undo_history_push(Undo_history* hist, Field* field, Usz tick_num) {
  if (hist->limit == 0)
    return;
  Undo_node* new_node;
  if (hist->count == hist->limit) {
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

void undo_history_apply(Undo_history* hist, Field* out_field,
                        Usz* out_tick_num) {
  Undo_node* last = hist->last;
  if (!last)
    return;
  field_copy(&last->field, out_field);
  *out_tick_num = last->tick_num;
}

Usz undo_history_count(Undo_history* hist) { return hist->count; }

void print_activity_indicator(WINDOW* win, Usz activity_counter) {
  // 7 segments that can each light up as Colors different colors.
  // This gives us Colors^Segments total configurations.
  enum { Segments = 7, Colors = 4 };
  Usz states = 1; // calculate Colors^Segments
  for (Usz i = 0; i < Segments; ++i)
    states *= Colors;
  // Wrap the counter to the range of displayable configurations.
  Usz val = activity_counter % states;
  chtype lamps[Colors];
#if 1 // Appearance where segments are always lit
  lamps[0] = ACS_HLINE | fg_bg(C_black, C_natural) | A_bold;
  lamps[1] = ACS_HLINE | fg_bg(C_white, C_natural) | A_normal;
  lamps[2] = ACS_HLINE | A_bold;
  lamps[3] = lamps[1];
#elif 0 // Brighter appearance where segments are always lit
  lamps[0] = ACS_HLINE | fg_bg(C_black, C_natural) | A_bold;
  lamps[1] = ACS_HLINE | A_normal;
  lamps[2] = ACS_HLINE | A_bold;
  lamps[3] = lamps[1];
#else   // Appearance where segments can turn off completely
  lamps[0] = ' ';
  lamps[1] = ACS_HLINE | fg_bg(C_black, C_natural) | A_bold;
  lamps[2] = ACS_HLINE | A_normal;
  lamps[3] = lamps[1];
#endif
  chtype buffer[Segments];
  for (Usz i = 0; i < Segments; ++i) {
    // Instead of a left-to-right, straightforward ascending least-to-most
    // significant digits display, we'll display it as a spiral.
    Usz j = i % 2 ? (6 - i / 2) : (i / 2);
    buffer[j] = lamps[val % Colors];
    val = val / Colors;
  }
  waddchnstr(win, buffer, Segments);
  // If you want to see what various combinations of colors and attributes look
  // like in different terminals.
#if 0
  waddch(win, 'a' | fg_bg(C_black, C_natural) | A_dim);
  waddch(win, 'b' | fg_bg(C_black, C_natural) | A_normal);
  waddch(win, 'c' | fg_bg(C_black, C_natural) | A_bold);
  waddch(win, 'd' | A_dim);
  waddch(win, 'e' | A_normal);
  waddch(win, 'f' | A_bold);
  waddch(win, 'g' | fg_bg(C_white, C_natural) | A_dim);
  waddch(win, 'h' | fg_bg(C_white, C_natural) | A_normal);
  waddch(win, 'i' | fg_bg(C_white, C_natural) | A_bold);
#endif
}

void advance_faketab(WINDOW* win, int offset_x, int tabstop) {
  if (tabstop < 1)
    return;
  int y, x, h, w;
  getyx(win, y, x);
  getmaxyx(win, h, w);
  (void)h;
  x = ((x + tabstop - 1) / tabstop) * tabstop + offset_x % tabstop;
  if (w < 1)
    w = 1;
  if (x >= w)
    x = w - 1;
  wmove(win, y, x);
}

void draw_hud(WINDOW* win, int win_y, int win_x, int height, int width,
              char const* filename, Usz field_h, Usz field_w,
              Usz ruler_spacing_y, Usz ruler_spacing_x, Usz tick_num, Usz bpm,
              Ged_cursor const* ged_cursor, Ged_input_mode input_mode,
              Usz activity_counter) {
  (void)height;
  (void)width;
  enum { Tabstop = 8 };
  wmove(win, win_y, win_x);
  wprintw(win, "%zux%zu", field_w, field_h);
  advance_faketab(win, win_x, Tabstop);
  wprintw(win, "%zu/%zu", ruler_spacing_x, ruler_spacing_y);
  advance_faketab(win, win_x, Tabstop);
  wprintw(win, "%zuf", tick_num);
  advance_faketab(win, win_x, Tabstop);
  wprintw(win, "%zu", bpm);
  advance_faketab(win, win_x, Tabstop);
  print_activity_indicator(win, activity_counter);
  wmove(win, win_y + 1, win_x);
  wprintw(win, "%zu,%zu", ged_cursor->x, ged_cursor->y);
  advance_faketab(win, win_x, Tabstop);
  wprintw(win, "%zu:%zu", ged_cursor->w, ged_cursor->h);
  advance_faketab(win, win_x, Tabstop);
  switch (input_mode) {
  case Ged_input_mode_normal:
    wattrset(win, A_normal);
    waddstr(win, "insert");
    break;
  case Ged_input_mode_append:
    wattrset(win, A_bold);
    waddstr(win, "append");
    break;
  case Ged_input_mode_selresize:
    wattrset(win, A_bold);
    waddstr(win, "select");
    break;
  case Ged_input_mode_slide:
    wattrset(win, A_reverse);
    waddstr(win, "slide");
    break;
  }
  advance_faketab(win, win_x, Tabstop);
  wattrset(win, A_normal);
  waddstr(win, filename);
}

void draw_glyphs_grid(WINDOW* win, int draw_y, int draw_x, int draw_h,
                      int draw_w, Glyph const* restrict gbuffer,
                      Mark const* restrict mbuffer, Usz field_h, Usz field_w,
                      Usz offset_y, Usz offset_x, Usz ruler_spacing_y,
                      Usz ruler_spacing_x) {
  assert(draw_y >= 0 && draw_x >= 0);
  assert(draw_h >= 0 && draw_w >= 0);
  enum { Bufcount = 4096 };
  chtype chbuffer[Bufcount];
  // todo buffer limit
  if (offset_y >= field_h || offset_x >= field_w)
    return;
  if (draw_y >= draw_h || draw_x >= draw_w)
    return;
  Usz rows = (Usz)(draw_h - draw_y);
  if (field_h - offset_y < rows)
    rows = field_h - offset_y;
  Usz cols = (Usz)(draw_w - draw_x);
  if (field_w - offset_x < cols)
    cols = field_w - offset_x;
  if (Bufcount < cols)
    cols = Bufcount;
  if (rows == 0 || cols == 0)
    return;
  bool use_rulers = ruler_spacing_y != 0 && ruler_spacing_x != 0;
  chtype bullet = ACS_BULLET;
  enum { T = 1 << 0, B = 1 << 1, L = 1 << 2, R = 1 << 3 };
  chtype rs[(T | B | L | R) + 1];
  if (use_rulers) {
    bool use_fancy_rulers = true;
    for (Usz i = 0; i < sizeof rs / sizeof(chtype); ++i) {
      rs[i] = '+';
    }
    if (use_fancy_rulers) {
      rs[T | L] = ACS_ULCORNER;
      rs[T | R] = ACS_URCORNER;
      rs[B | L] = ACS_LLCORNER;
      rs[B | R] = ACS_LRCORNER;
      rs[T] = ACS_TTEE;
      rs[B] = ACS_BTEE;
      rs[L] = ACS_LTEE;
      rs[R] = ACS_RTEE;
    }
  }
  for (Usz iy = 0; iy < rows; ++iy) {
    Usz line_offset = (offset_y + iy) * field_w + offset_x;
    Glyph const* g_row = gbuffer + line_offset;
    Mark const* m_row = mbuffer + line_offset;
    bool use_y_ruler = use_rulers && (iy + offset_y) % ruler_spacing_y == 0;
    for (Usz ix = 0; ix < cols; ++ix) {
      Glyph g = g_row[ix];
      Mark m = m_row[ix];
      chtype ch;
      if (g == '.') {
        if (use_y_ruler && (ix + offset_x) % ruler_spacing_x == 0) {
          int p = 0; // clang-format off
          if (iy + offset_y     == 0      ) p |= T;
          if (iy + offset_y + 1 == field_h) p |= B;
          if (ix + offset_x     == 0      ) p |= L;
          if (ix + offset_x + 1 == field_w) p |= R;
          ch = rs[p]; // clang-format on
        } else {
          ch = bullet;
        }
      } else {
        ch = (chtype)g;
      }
      attr_t attrs = term_attrs_of_cell(g, m);
      chbuffer[ix] = ch | attrs;
    }
    wmove(win, draw_y + (int)iy, draw_x);
    waddchnstr(win, chbuffer, (int)cols);
  }
}

void draw_glyphs_grid_scrolled(WINDOW* win, int draw_y, int draw_x, int draw_h,
                               int draw_w, Glyph const* restrict gbuffer,
                               Mark const* restrict mbuffer, Usz field_h,
                               Usz field_w, int scroll_y, int scroll_x,
                               Usz ruler_spacing_y, Usz ruler_spacing_x) {
  if (scroll_y < 0) {
    draw_y += -scroll_y;
    scroll_y = 0;
  }
  if (scroll_x < 0) {
    draw_x += -scroll_x;
    scroll_x = 0;
  }
  draw_glyphs_grid(win, draw_y, draw_x, draw_h, draw_w, gbuffer, mbuffer,
                   field_h, field_w, (Usz)scroll_y, (Usz)scroll_x,
                   ruler_spacing_y, ruler_spacing_x);
}

void ged_cursor_confine(Ged_cursor* tc, Usz height, Usz width) {
  if (height == 0 || width == 0)
    return;
  if (tc->y >= height)
    tc->y = height - 1;
  if (tc->x >= width)
    tc->x = width - 1;
}

void draw_oevent_list(WINDOW* win, Oevent_list const* oevent_list) {
  wmove(win, 0, 0);
  int win_h = getmaxy(win);
  wprintw(win, "Count: %d", (int)oevent_list->count);
  for (Usz i = 0, num_events = oevent_list->count; i < num_events; ++i) {
    int cury = getcury(win);
    if (cury + 1 >= win_h)
      return;
    wmove(win, cury + 1, 0);
    Oevent const* ev = oevent_list->buffer + i;
    Oevent_types evt = ev->any.oevent_type;
    switch (evt) {
    case Oevent_type_midi: {
      Oevent_midi const* em = &ev->midi;
      wprintw(win,
              "MIDI\tchannel %d\toctave %d\tnote %d\tvelocity %d\tlength %d",
              (int)em->channel, (int)em->octave, (int)em->note,
              (int)em->velocity, (int)em->bar_divisor);
    } break;
    case Oevent_type_osc_ints: {
      Oevent_osc_ints const* eo = &ev->osc_ints;
      wprintw(win, "OSC\t%c\tcount: %d ", eo->glyph, eo->count, eo->count);
      waddch(win, ACS_VLINE);
      for (Usz j = 0; j < eo->count; ++j) {
        wprintw(win, " %d", eo->numbers[j]);
      }
    } break;
    case Oevent_type_udp_string: {
      Oevent_udp_string const* eo = &ev->udp_string;
      wprintw(win, "UDP\tcount %d\t", (int)eo->count);
      for (Usz j = 0; j < (Usz)eo->count; ++j) {
        waddch(win, (chtype)eo->chars[j]);
      }
    } break;
    }
  }
}

void ged_resize_grid(Field* field, Mbuf_reusable* mbr, Usz new_height,
                     Usz new_width, Usz tick_num, Field* scratch_field,
                     Undo_history* undo_hist, Ged_cursor* ged_cursor) {
  assert(new_height > 0 && new_width > 0);
  undo_history_push(undo_hist, field, tick_num);
  field_copy(field, scratch_field);
  field_resize_raw(field, new_height, new_width);
  // junky copies until i write a smarter thing
  memset(field->buffer, '.', new_height * new_width * sizeof(Glyph));
  gbuffer_copy_subrect(scratch_field->buffer, field->buffer,
                       scratch_field->height, scratch_field->width,
                       field->height, field->width, 0, 0, 0, 0,
                       scratch_field->height, scratch_field->width);
  ged_cursor_confine(ged_cursor, new_height, new_width);
  mbuf_reusable_ensure_size(mbr, new_height, new_width);
}

static Usz adjust_rulers_humanized(Usz ruler, Usz in, Isz delta_rulers) {
  // slightly more confusing because desired grid sizes are +1 (e.g. ruler of
  // length 8 wants to snap to 25 and 33, not 24 and 32). also this math is
  // sloppy.
  assert(ruler > 0);
  if (in == 0) {
    return delta_rulers > 0 ? ruler * (Usz)delta_rulers : 1;
  }
  // could overflow if inputs are big
  if (delta_rulers < 0)
    in += ruler - 1;
  Isz n = ((Isz)in - 1) / (Isz)ruler + delta_rulers;
  if (n < 0)
    n = 0;
  return ruler * (Usz)n + 1;
}

// Resizes by number of ruler divisions, and snaps size to closest division in
// a way a human would expect. Adds +1 to the output, so grid resulting size is
// 1 unit longer than the actual ruler length.
bool ged_resize_grid_snap_ruler(Field* field, Mbuf_reusable* mbr, Usz ruler_y,
                                Usz ruler_x, Isz delta_h, Isz delta_w,
                                Usz tick_num, Field* scratch_field,
                                Undo_history* undo_hist,
                                Ged_cursor* ged_cursor) {
  assert(ruler_y > 0);
  assert(ruler_x > 0);
  Usz field_h = field->height;
  Usz field_w = field->width;
  assert(field_h > 0);
  assert(field_w > 0);
  if (ruler_y == 0 || ruler_x == 0 || field_h == 0 || field_w == 0)
    return false;
  Usz new_field_h = field_h;
  Usz new_field_w = field_w;
  if (delta_h != 0)
    new_field_h = adjust_rulers_humanized(ruler_y, field_h, delta_h);
  if (delta_w != 0)
    new_field_w = adjust_rulers_humanized(ruler_x, field_w, delta_w);
  if (new_field_h > ORCA_Y_MAX)
    new_field_h = ORCA_Y_MAX;
  if (new_field_w > ORCA_X_MAX)
    new_field_w = ORCA_X_MAX;
  if (new_field_h == field_h && new_field_w == field_w)
    return false;
  ged_resize_grid(field, mbr, new_field_h, new_field_w, tick_num, scratch_field,
                  undo_hist, ged_cursor);
  return true;
}

typedef enum {
  Midi_mode_type_null,
  Midi_mode_type_osc_bidule,
#ifdef FEAT_PORTMIDI
  Midi_mode_type_portmidi,
#endif
} Midi_mode_type;

typedef struct {
  Midi_mode_type type;
} Midi_mode_any;

typedef struct {
  Midi_mode_type type;
  char const* path;
} Midi_mode_osc_bidule;

#ifdef FEAT_PORTMIDI
typedef struct {
  Midi_mode_type type;
  PmDeviceID device_id;
  PortMidiStream* stream;
} Midi_mode_portmidi;
// Not sure whether it's OK to call Pm_Terminate() without having a successful
// call to Pm_Initialize() -- let's just treat it with tweezers.
static bool portmidi_is_initialized = false;
#endif

typedef union {
  Midi_mode_any any;
  Midi_mode_osc_bidule osc_bidule;
#ifdef FEAT_PORTMIDI
  Midi_mode_portmidi portmidi;
#endif
} Midi_mode;

void midi_mode_init_null(Midi_mode* mm) { mm->any.type = Midi_mode_type_null; }
void midi_mode_init_osc_bidule(Midi_mode* mm, char const* path) {
  mm->osc_bidule.type = Midi_mode_type_osc_bidule;
  mm->osc_bidule.path = path;
}
#ifdef FEAT_PORTMIDI
PmError portmidi_init_if_necessary(void) {
  if (portmidi_is_initialized)
    return 0;
  // U64 t0 = stm_now();
  PmError e = Pm_Initialize();
  // fprintf(stderr, "ms: %f\n", stm_sec(stm_since(t0)) * 1000);
  if (e)
    return e;
  portmidi_is_initialized = true;
  return 0;
}
PmError midi_mode_init_portmidi(Midi_mode* mm, PmDeviceID dev_id) {
  PmError e = portmidi_init_if_necessary();
  if (e)
    return e;
  e = Pm_OpenOutput(&mm->portmidi.stream, dev_id, NULL, 0, NULL, NULL, 0);
  if (e)
    return e;
  mm->portmidi.type = Midi_mode_type_portmidi;
  mm->portmidi.device_id = dev_id;
  return pmNoError;
}
#endif
void midi_mode_deinit(Midi_mode* mm) {
  switch (mm->any.type) {
  case Midi_mode_type_null:
  case Midi_mode_type_osc_bidule:
    break;
#ifdef FEAT_PORTMIDI
  case Midi_mode_type_portmidi: {
    Pm_Close(mm->portmidi.stream);
  } break;
#endif
  }
}

typedef struct {
  Field field;
  Field scratch_field;
  Field clipboard_field;
  Mbuf_reusable mbuf_r;
  Undo_history undo_hist;
  Oevent_list oevent_list;
  Oevent_list scratch_oevent_list;
  Susnote_list susnote_list;
  Ged_cursor ged_cursor;
  Usz tick_num;
  Usz ruler_spacing_y, ruler_spacing_x;
  Ged_input_mode input_mode;
  Usz bpm;
  U64 clock;
  double accum_secs;
  double time_to_next_note_off;
  char const* filename;
  Oosc_dev* oosc_dev;
  Midi_mode const* midi_mode;
  Usz activity_counter;
  Usz random_seed;
  Usz drag_start_y, drag_start_x;
  int win_h, win_w;
  int softmargin_y, softmargin_x;
  int grid_h;
  int grid_scroll_y, grid_scroll_x; // not sure if i like this being int
  bool needs_remarking : 1;
  bool is_draw_dirty : 1;
  bool is_playing : 1;
  bool draw_event_list : 1;
  bool is_mouse_down : 1;
  bool is_mouse_dragging : 1;
  bool is_hud_visible : 1;
} Ged;

void ged_init(Ged* a, Usz undo_limit, Usz init_bpm, Usz init_seed) {
  field_init(&a->field);
  field_init(&a->scratch_field);
  field_init(&a->clipboard_field);
  mbuf_reusable_init(&a->mbuf_r);
  undo_history_init(&a->undo_hist, undo_limit);
  oevent_list_init(&a->oevent_list);
  oevent_list_init(&a->scratch_oevent_list);
  susnote_list_init(&a->susnote_list);
  ged_cursor_init(&a->ged_cursor);
  a->tick_num = 0;
  a->ruler_spacing_y = a->ruler_spacing_x = 8;
  a->input_mode = Ged_input_mode_normal;
  a->bpm = init_bpm;
  a->clock = 0;
  a->accum_secs = 0.0;
  a->time_to_next_note_off = 1.0;
  a->filename = NULL;
  a->oosc_dev = NULL;
  a->midi_mode = NULL;
  a->activity_counter = 0;
  a->random_seed = init_seed;
  a->drag_start_y = a->drag_start_x = 0;
  a->win_h = a->win_w = 0;
  a->softmargin_y = a->softmargin_x = 0;
  a->grid_h = 0;
  a->grid_scroll_y = a->grid_scroll_x = 0;
  a->needs_remarking = true;
  a->is_draw_dirty = false;
  a->is_playing = true;
  a->draw_event_list = false;
  a->is_mouse_down = false;
  a->is_mouse_dragging = false;
  a->is_hud_visible = false;
}

void ged_deinit(Ged* a) {
  field_deinit(&a->field);
  field_deinit(&a->scratch_field);
  field_deinit(&a->clipboard_field);
  mbuf_reusable_deinit(&a->mbuf_r);
  undo_history_deinit(&a->undo_hist);
  oevent_list_deinit(&a->oevent_list);
  oevent_list_deinit(&a->scratch_oevent_list);
  susnote_list_deinit(&a->susnote_list);
  if (a->oosc_dev) {
    oosc_dev_destroy(a->oosc_dev);
  }
}

bool ged_is_draw_dirty(Ged* a) {
  return a->is_draw_dirty || a->needs_remarking;
}

bool ged_set_osc_udp(Ged* a, char const* dest_addr, char const* dest_port) {
  if (a->oosc_dev) {
    oosc_dev_destroy(a->oosc_dev);
    a->oosc_dev = NULL;
  }
  if (dest_port) {
    Oosc_udp_create_error err =
        oosc_dev_create_udp(&a->oosc_dev, dest_addr, dest_port);
    if (err) {
      return false;
    }
  }
  return true;
}

void ged_set_midi_mode(Ged* a, Midi_mode const* midi_mode) {
  a->midi_mode = midi_mode;
}

void send_midi_note_offs(Oosc_dev* oosc_dev, Midi_mode const* midi_mode,
                         Susnote const* start, Susnote const* end) {
  Midi_mode_type midi_mode_type = midi_mode->any.type;
  for (; start != end; ++start) {
#if 0
    float under = start->remaining;
    if (under < 0.0) {
      fprintf(stderr, "cutoff slop: %f\n", under);
    }
#endif
    U16 chan_note = start->chan_note;
    Usz chan = chan_note >> 8u;
    Usz note = chan_note & 0xFFu;
    switch (midi_mode_type) {
    case Midi_mode_type_null:
      break;
    case Midi_mode_type_osc_bidule: {
      if (!oosc_dev)
        continue;
      I32 ints[3];
      ints[0] = (0x8 << 4) | (U8)chan; // status
      ints[1] = (I32)note;             // note number
      ints[2] = 0;                     // velocity
      oosc_send_int32s(oosc_dev, midi_mode->osc_bidule.path, ints,
                       ORCA_ARRAY_COUNTOF(ints));
    } break;
#ifdef FEAT_PORTMIDI
    case Midi_mode_type_portmidi: {
      int istatus = (0x8 << 4) | (int)chan;
      int inote = (int)note;
      int ivel = 0;
      Pm_WriteShort(midi_mode->portmidi.stream, 0,
                    Pm_Message(istatus, inote, ivel));
    } break;
#endif
    }
  }
}

void send_control_message(Oosc_dev* oosc_dev, char const* osc_address) {
  if (!oosc_dev)
    return;
  oosc_send_int32s(oosc_dev, osc_address, NULL, 0);
}

void send_num_message(Oosc_dev* oosc_dev, char const* osc_address, I32 num) {
  if (!oosc_dev)
    return;
  I32 nums[1];
  nums[0] = num;
  oosc_send_int32s(oosc_dev, osc_address, nums, ORCA_ARRAY_COUNTOF(nums));
}

void apply_time_to_sustained_notes(Oosc_dev* oosc_dev,
                                   Midi_mode const* midi_mode,
                                   double time_elapsed,
                                   Susnote_list* susnote_list,
                                   double* next_note_off_deadline) {
  Usz start_removed, end_removed;
  susnote_list_advance_time(susnote_list, time_elapsed, &start_removed,
                            &end_removed, next_note_off_deadline);
  if (ORCA_UNLIKELY(start_removed != end_removed)) {
    Susnote const* restrict susnotes_off = susnote_list->buffer;
    send_midi_note_offs(oosc_dev, midi_mode, susnotes_off + start_removed,
                        susnotes_off + end_removed);
  }
}

void ged_stop_all_sustained_notes(Ged* a) {
  Susnote_list* sl = &a->susnote_list;
  send_midi_note_offs(a->oosc_dev, a->midi_mode, sl->buffer,
                      sl->buffer + sl->count);
  susnote_list_clear(sl);
  a->time_to_next_note_off = 1.0;
}

void send_output_events(Oosc_dev* oosc_dev, Midi_mode const* midi_mode, Usz bpm,
                        Susnote_list* susnote_list, Oevent const* events,
                        Usz count) {
  Midi_mode_type midi_mode_type = midi_mode->any.type;
  double bar_secs = 60.0 / (double)bpm * 4.0;

  enum { Midi_on_capacity = 512 };
  typedef struct {
    U8 channel;
    U8 note_number;
    U8 velocity;
  } Midi_note_on;
  Midi_note_on midi_note_ons[Midi_on_capacity];
  Susnote new_susnotes[Midi_on_capacity];
  Usz midi_note_count = 0;

  for (Usz i = 0; i < count; ++i) {
    if (midi_note_count == Midi_on_capacity)
      break;
    Oevent const* e = events + i;
    switch ((Oevent_types)e->any.oevent_type) {
    case Oevent_type_midi: {
      Oevent_midi const* em = &e->midi;
      Usz note_number = (Usz)(12u * em->octave + em->note);
      Usz channel = em->channel;
      Usz bar_div = em->bar_divisor;
      midi_note_ons[midi_note_count] =
          (Midi_note_on){.channel = (U8)channel,
                         .note_number = (U8)note_number,
                         .velocity = em->velocity};
      new_susnotes[midi_note_count] = (Susnote){
          .remaining =
              bar_div == 0 ? 0.0f : (float)(bar_secs / (double)bar_div),
          .chan_note = (U16)((channel << 8u) | note_number)};
#if 0
      fprintf(stderr, "bar div: %d, time: %f\n", (int)bar_div,
              new_susnotes[midi_note_count].remaining);
#endif
      ++midi_note_count;
    } break;
    case Oevent_type_osc_ints: {
      // kinda lame
      if (!oosc_dev)
        continue;
      Oevent_osc_ints const* eo = &e->osc_ints;
      char path_buff[3];
      path_buff[0] = '/';
      path_buff[1] = eo->glyph;
      path_buff[2] = 0;
      I32 ints[ORCA_ARRAY_COUNTOF(eo->numbers)];
      Usz nnum = eo->count;
      for (Usz inum = 0; inum < nnum; ++inum) {
        ints[inum] = eo->numbers[inum];
      }
      oosc_send_int32s(oosc_dev, path_buff, ints, nnum);
    } break;
    case Oevent_type_udp_string: {
      if (!oosc_dev)
        continue;
      Oevent_udp_string const* eo = &e->udp_string;
      oosc_send_datagram(oosc_dev, eo->chars, eo->count);
    } break;
    }
  }

  if (midi_note_count > 0 && midi_mode) {
    Usz start_note_offs, end_note_offs;
    susnote_list_add_notes(susnote_list, new_susnotes, midi_note_count,
                           &start_note_offs, &end_note_offs);
    if (start_note_offs != end_note_offs) {
      Susnote const* restrict susnotes_off = susnote_list->buffer;
      send_midi_note_offs(oosc_dev, midi_mode, susnotes_off + start_note_offs,
                          susnotes_off + end_note_offs);
    }
    for (Usz i = 0; i < midi_note_count; ++i) {
      Midi_note_on mno = midi_note_ons[i];
      switch (midi_mode_type) {
      case Midi_mode_type_null:
        break;
      case Midi_mode_type_osc_bidule: {
        if (!oosc_dev)
          continue; // not sure if needed
        I32 ints[3];
        ints[0] = (0x9 << 4) | mno.channel; // status
        ints[1] = mno.note_number;          // note number
        ints[2] = mno.velocity;             // velocity
        oosc_send_int32s(oosc_dev, midi_mode->osc_bidule.path, ints,
                         ORCA_ARRAY_COUNTOF(ints));
      } break;
#ifdef FEAT_PORTMIDI
      case Midi_mode_type_portmidi: {
        int istatus = (0x9 << 4) | (int)mno.channel;
        int inote = (int)mno.note_number;
        int ivel = (int)mno.velocity;
        PmError pme = Pm_WriteShort(midi_mode->portmidi.stream, 0,
                                    Pm_Message(istatus, inote, ivel));
        // todo bad
        if (pme) {
          fprintf(stderr, "PortMidi error: %s\n", Pm_GetErrorText(pme));
        }
      } break;
#endif
      }
    }
  }
}

static double ms_to_sec(double ms) { return ms / 1000.0; }

double ged_secs_to_deadline(Ged const* a) {
  if (a->is_playing) {
    double secs_span = 60.0 / (double)a->bpm / 4.0;
    double rem = secs_span - (stm_sec(stm_since(a->clock)) + a->accum_secs);
    double next_note_off = a->time_to_next_note_off;
    if (rem < 0.0)
      rem = 0.0;
    else if (next_note_off < rem)
      rem = next_note_off;
    return rem;
  } else {
    return 1.0;
  }
}

void ged_reset_clock(Ged* a) { a->clock = stm_now(); }

void clear_and_run_vm(Glyph* restrict gbuf, Mark* restrict mbuf, Usz height,
                      Usz width, Usz tick_number, Oevent_list* oevent_list,
                      Usz random_seed) {
  mbuffer_clear(mbuf, height, width);
  oevent_list_clear(oevent_list);
  orca_run(gbuf, mbuf, height, width, tick_number, oevent_list, random_seed);
}

void ged_do_stuff(Ged* a) {
  double secs_span = 60.0 / (double)a->bpm / 4.0;
  Oosc_dev* oosc_dev = a->oosc_dev;
  Midi_mode const* midi_mode = a->midi_mode;
  double secs = stm_sec(stm_since(a->clock));
  (void)secs; // unused, was previously used for activity meter decay
  if (!a->is_playing)
    return;
  bool do_play = false;
#if TIME_DEBUG
  Usz spins = 0;
  U64 spin_start = stm_now();
#endif
  for (;;) {
    U64 now = stm_now();
    U64 diff = stm_diff(now, a->clock);
    double sdiff = stm_sec(diff) + a->accum_secs;
    if (sdiff >= secs_span) {
      a->clock = now;
      a->accum_secs = sdiff - secs_span;
#if TIME_DEBUG
      if (a->accum_secs > 0.000001) {
        fprintf(stderr, "err: %f\n", a->accum_secs);
        if (a->accum_secs > 0.00005) {
          fprintf(stderr, "guilty timeout: %d\n", spin_track_timeout);
        }
      }
#endif
      do_play = true;
      break;
    }
    if (secs_span - sdiff > ms_to_sec(0.1))
      break;
#if TIME_DEBUG
    ++spins;
#endif
  }
#if TIME_DEBUG
  if (spins > 0) {
    fprintf(stderr, "%d spins in %f us with timeout %d\n", (int)spins,
            stm_us(stm_since(spin_start)), spin_track_timeout);
  }
#endif
  if (do_play) {
    apply_time_to_sustained_notes(oosc_dev, midi_mode, secs_span,
                                  &a->susnote_list, &a->time_to_next_note_off);
    clear_and_run_vm(a->field.buffer, a->mbuf_r.buffer, a->field.height,
                     a->field.width, a->tick_num, &a->oevent_list,
                     a->random_seed);
    ++a->tick_num;
    a->needs_remarking = true;
    a->is_draw_dirty = true;

    Usz count = a->oevent_list.count;
    if (count > 0) {
      send_output_events(oosc_dev, midi_mode, a->bpm, &a->susnote_list,
                         a->oevent_list.buffer, count);
      a->activity_counter += count;
    }
    // note for future: sustained note deadlines may have changed due to note
    // on. will need to update stored deadline in memory if
    // ged_apply_delta_secs isn't called again immediately after ged_do_stuff.
  }
}

static inline Isz isz_clamp(Isz x, Isz low, Isz high) {
  return x < low ? low : x > high ? high : x;
}

// todo cleanup to use proper unsigned/signed w/ overflow check
Isz scroll_offset_on_axis_for_cursor_pos(Isz win_len, Isz cont_len,
                                         Isz cursor_pos, Isz pad,
                                         Isz cur_scroll) {
  if (win_len <= 0 || cont_len <= 0)
    return 0;
  if (cont_len <= win_len)
    return -((win_len - cont_len) / 2);
  if (pad * 2 >= win_len) {
    pad = (win_len - 1) / 2;
  }
  Isz min_vis_scroll = cursor_pos - win_len + 1 + pad;
  Isz max_vis_scroll = cursor_pos - pad;
  Isz new_scroll;
  if (cur_scroll < min_vis_scroll)
    new_scroll = min_vis_scroll;
  else if (cur_scroll > max_vis_scroll)
    new_scroll = max_vis_scroll;
  else
    new_scroll = cur_scroll;
  return isz_clamp(new_scroll, 0, cont_len - win_len);
}

void ged_make_cursor_visible(Ged* a) {
  int grid_h = a->grid_h;
  int cur_scr_y = a->grid_scroll_y;
  int cur_scr_x = a->grid_scroll_x;
  int new_scr_y = (int)scroll_offset_on_axis_for_cursor_pos(
      grid_h, (Isz)a->field.height, (Isz)a->ged_cursor.y, 5, cur_scr_y);
  int new_scr_x = (int)scroll_offset_on_axis_for_cursor_pos(
      a->win_w, (Isz)a->field.width, (Isz)a->ged_cursor.x, 5, cur_scr_x);
  if (new_scr_y == cur_scr_y && new_scr_x == cur_scr_x)
    return;
  a->grid_scroll_y = new_scr_y;
  a->grid_scroll_x = new_scr_x;
  a->is_draw_dirty = true;
}

enum { Hud_height = 2 };

void ged_update_internal_geometry(Ged* a) {
  int win_h = a->win_h;
  int softmargin_y = a->softmargin_y;
  bool show_hud = win_h > Hud_height + 1;
  int grid_h = show_hud ? win_h - Hud_height : win_h;
  if (grid_h > a->field.height) {
    int halfy = (grid_h - a->field.height + 1) / 2;
    grid_h -= halfy < softmargin_y ? halfy : softmargin_y;
  }
  a->grid_h = grid_h;
  a->is_draw_dirty = true;
  a->is_hud_visible = show_hud;
}

void ged_set_window_size(Ged* a, int win_h, int win_w, int softmargin_y,
                         int softmargin_x) {
  if (a->win_h == win_h && a->win_w == win_w &&
      a->softmargin_y == softmargin_y && a->softmargin_x == softmargin_x)
    return;
  a->win_h = win_h;
  a->win_w = win_w;
  a->softmargin_y = softmargin_y;
  a->softmargin_x = softmargin_x;
  ged_update_internal_geometry(a);
  ged_make_cursor_visible(a);
}

bool ged_suggest_nice_grid_size(int win_h, int win_w, int softmargin_y,
                                int softmargin_x, int ruler_spacing_y,
                                int ruler_spacing_x, Usz* out_grid_h,
                                Usz* out_grid_w) {
  if (win_h < 1 || win_w < 1 || softmargin_y < 0 || softmargin_x < 0 ||
      ruler_spacing_y < 1 || ruler_spacing_x < 1)
    return false;
  // TODO overflow checks
  int h = (win_h - softmargin_y - Hud_height - 1) / ruler_spacing_y;
  h *= ruler_spacing_y;
  int w = (win_w - softmargin_x * 2 - 1) / ruler_spacing_x;
  w *= ruler_spacing_x;
  if (h < ruler_spacing_y)
    h = ruler_spacing_y;
  if (w < ruler_spacing_x)
    w = ruler_spacing_x;
  h++;
  w++;
  if (h >= ORCA_Y_MAX || w >= ORCA_X_MAX)
    return false;
  *out_grid_h = (Usz)h;
  *out_grid_w = (Usz)w;
  return true;
}
bool ged_suggest_tight_grid_size(int win_h, int win_w, int softmargin_y,
                                 int softmargin_x, Usz* out_grid_h,
                                 Usz* out_grid_w) {

  if (win_h < 1 || win_w < 1 || softmargin_y < 0 || softmargin_x < 0)
    return false;
  // TODO overflow checks
  int h = win_h - softmargin_y - Hud_height;
  int w = win_w - softmargin_x * 2;
  if (h < 1 || w < 1 || h >= ORCA_Y_MAX || w >= ORCA_X_MAX)
    return false;
  *out_grid_h = (Usz)h;
  *out_grid_w = (Usz)w;
  return true;
}

void ged_draw(Ged* a, WINDOW* win) {
  // We can predictavely step the next simulation tick and then use the
  // resulting mark buffer for better UI visualization. If we don't do this,
  // after loading a fresh file or after the user performs some edit (or even
  // after a regular simulation step), the new glyph buffer won't have had
  // phase 0 of the simulation run, which means the ports and other flags won't
  // be set on the mark buffer, so the colors for disabled cells, ports, etc.
  // won't be set.
  //
  // We can just perform a simulation step using the current state, keep the
  // mark buffer that it produces, then roll back the glyph buffer to where it
  // was before. This should produce results similar to having specialized UI
  // code that looks at each glyph and figures out the ports, etc.
  if (a->needs_remarking) {
    field_resize_raw_if_necessary(&a->scratch_field, a->field.height,
                                  a->field.width);
    field_copy(&a->field, &a->scratch_field);
    mbuf_reusable_ensure_size(&a->mbuf_r, a->field.height, a->field.width);
    clear_and_run_vm(a->scratch_field.buffer, a->mbuf_r.buffer, a->field.height,
                     a->field.width, a->tick_num, &a->scratch_oevent_list,
                     a->random_seed);
    a->needs_remarking = false;
  }
  int win_w = a->win_w;
  draw_glyphs_grid_scrolled(win, 0, 0, a->grid_h, win_w, a->field.buffer,
                            a->mbuf_r.buffer, a->field.height, a->field.width,
                            a->grid_scroll_y, a->grid_scroll_x,
                            a->ruler_spacing_y, a->ruler_spacing_x);
  draw_grid_cursor(win, 0, 0, a->grid_h, win_w, a->field.buffer,
                   a->field.height, a->field.width, a->grid_scroll_y,
                   a->grid_scroll_x, a->ged_cursor.y, a->ged_cursor.x,
                   a->ged_cursor.h, a->ged_cursor.w, a->input_mode,
                   a->is_playing);
  if (a->is_hud_visible) {
    char const* filename = a->filename ? a->filename : "unnamed";
    // char const* filename =
    //     a->filename && strlen(a->filename) > 0 ? a->filename : "unnamed";
    int hud_x = win_w > 50 + a->softmargin_x * 2 ? a->softmargin_x : 0;
    draw_hud(win, a->grid_h, hud_x, Hud_height, win_w, filename,
             a->field.height, a->field.width, a->ruler_spacing_y,
             a->ruler_spacing_x, a->tick_num, a->bpm, &a->ged_cursor,
             a->input_mode, a->activity_counter);
  }
  if (a->draw_event_list) {
    draw_oevent_list(win, &a->oevent_list);
  }
  a->is_draw_dirty = false;
}

void ged_adjust_bpm(Ged* a, Isz delta_bpm) {
  Isz new_bpm = (Isz)a->bpm;
  if (delta_bpm < 0 || new_bpm < INT_MAX - delta_bpm)
    new_bpm += delta_bpm;
  else
    new_bpm = INT_MAX;
  if (new_bpm < 1)
    new_bpm = 1;
  if ((Usz)new_bpm != a->bpm) {
    a->bpm = (Usz)new_bpm;
    a->is_draw_dirty = true;
    send_num_message(a->oosc_dev, "/orca/bpm", (I32)new_bpm);
  }
}

void ged_move_cursor_relative(Ged* a, Isz delta_y, Isz delta_x) {
  ged_cursor_move_relative(&a->ged_cursor, a->field.height, a->field.width,
                           delta_y, delta_x);
  ged_make_cursor_visible(a);
  a->is_draw_dirty = true;
}

Usz guarded_selection_axis_resize(Usz x, int delta) {
  if (delta < 0) {
    if (delta > INT_MIN && (Usz)(-delta) < x) {
      x -= (Usz)(-delta);
    }
  } else if (x < SIZE_MAX - (Usz)delta) {
    x += (Usz)delta;
  }
  return x;
}

void ged_modify_selection_size(Ged* a, int delta_y, int delta_x) {
  Usz cur_h = a->ged_cursor.h;
  Usz cur_w = a->ged_cursor.w;
  Usz new_h = guarded_selection_axis_resize(cur_h, delta_y);
  Usz new_w = guarded_selection_axis_resize(cur_w, delta_x);
  if (cur_h != new_h || cur_w != new_w) {
    a->ged_cursor.h = new_h;
    a->ged_cursor.w = new_w;
    a->is_draw_dirty = true;
  }
}

bool ged_try_selection_clipped_to_field(Ged const* a, Usz* out_y, Usz* out_x,
                                        Usz* out_h, Usz* out_w) {
  Usz curs_y = a->ged_cursor.y;
  Usz curs_x = a->ged_cursor.x;
  Usz curs_h = a->ged_cursor.h;
  Usz curs_w = a->ged_cursor.w;
  Usz field_h = a->field.height;
  Usz field_w = a->field.width;
  if (curs_y >= field_h || curs_x >= field_w)
    return false;
  if (field_h - curs_y < curs_h)
    curs_h = field_h - curs_y;
  if (field_w - curs_x < curs_w)
    curs_w = field_w - curs_x;
  *out_y = curs_y;
  *out_x = curs_x;
  *out_h = curs_h;
  *out_w = curs_w;
  return true;
}

bool ged_slide_selection(Ged* a, int delta_y, int delta_x) {
  Usz curs_y_0, curs_x_0, curs_h_0, curs_w_0;
  Usz curs_y_1, curs_x_1, curs_h_1, curs_w_1;
  if (!ged_try_selection_clipped_to_field(a, &curs_y_0, &curs_x_0, &curs_h_0,
                                          &curs_w_0))
    return false;
  ged_move_cursor_relative(a, delta_y, delta_x);
  if (!ged_try_selection_clipped_to_field(a, &curs_y_1, &curs_x_1, &curs_h_1,
                                          &curs_w_1))
    return false;
  // Don't create a history entry if nothing is going to happen.
  if (curs_y_0 == curs_y_1 && curs_x_0 == curs_x_1 && curs_h_0 == curs_h_1 &&
      curs_w_0 == curs_w_1)
    return false;
  undo_history_push(&a->undo_hist, &a->field, a->tick_num);
  Usz field_h = a->field.height;
  Usz field_w = a->field.width;
  gbuffer_copy_subrect(a->field.buffer, a->field.buffer, field_h, field_w,
                       field_h, field_w, curs_y_0, curs_x_0, curs_y_1, curs_x_1,
                       curs_h_0, curs_w_0);
  // Erase/clear the area that was within the selection rectangle in the
  // starting position, but wasn't written to during the copy. (In other words,
  // this is the area that was 'left behind' when we moved the selection
  // rectangle, plus any area that was along the bottom and right edge of the
  // field that didn't have anything to copy to it when the selection rectangle
  // extended outside of the field.)
  Usz ey, eh, ex, ew;
  if (curs_y_1 > curs_y_0) {
    ey = curs_y_0;
    eh = curs_y_1 - curs_y_0;
  } else {
    ey = curs_y_1 + curs_h_0;
    eh = (curs_y_0 + curs_h_0) - ey;
  }
  if (curs_x_1 > curs_x_0) {
    ex = curs_x_0;
    ew = curs_x_1 - curs_x_0;
  } else {
    ex = curs_x_1 + curs_w_0;
    ew = (curs_x_0 + curs_w_0) - ex;
  }
  gbuffer_fill_subrect(a->field.buffer, field_h, field_w, ey, curs_x_0, eh,
                       curs_w_0, '.');
  gbuffer_fill_subrect(a->field.buffer, field_h, field_w, curs_y_0, ex,
                       curs_h_0, ew, '.');
  a->needs_remarking = true;
  return true;
}

typedef enum {
  Ged_dir_up,
  Ged_dir_down,
  Ged_dir_left,
  Ged_dir_right,
} Ged_dir;

void ged_dir_input(Ged* a, Ged_dir dir, int step_length) {
  switch (a->input_mode) {
  case Ged_input_mode_normal:
  case Ged_input_mode_append:
    switch (dir) {
    case Ged_dir_up:
      ged_move_cursor_relative(a, -step_length, 0);
      break;
    case Ged_dir_down:
      ged_move_cursor_relative(a, step_length, 0);
      break;
    case Ged_dir_left:
      ged_move_cursor_relative(a, 0, -step_length);
      break;
    case Ged_dir_right:
      ged_move_cursor_relative(a, 0, step_length);
      break;
    }
    break;
  case Ged_input_mode_selresize:
    switch (dir) {
    case Ged_dir_up:
      ged_modify_selection_size(a, -step_length, 0);
      break;
    case Ged_dir_down:
      ged_modify_selection_size(a, step_length, 0);
      break;
    case Ged_dir_left:
      ged_modify_selection_size(a, 0, -step_length);
      break;
    case Ged_dir_right:
      ged_modify_selection_size(a, 0, step_length);
      break;
    }
    break;
  case Ged_input_mode_slide:
    switch (dir) {
    case Ged_dir_up:
      ged_slide_selection(a, -step_length, 0);
      break;
    case Ged_dir_down:
      ged_slide_selection(a, step_length, 0);
      break;
    case Ged_dir_left:
      ged_slide_selection(a, 0, -step_length);
      break;
    case Ged_dir_right:
      ged_slide_selection(a, 0, step_length);
      break;
    }
    break;
  }
}

Usz view_to_scrolled_grid(Usz field_len, Usz visual_coord, int scroll_offset) {
  if (field_len == 0)
    return 0;
  if (scroll_offset < 0) {
    if ((Usz)(-scroll_offset) <= visual_coord) {
      visual_coord -= (Usz)(-scroll_offset);
    } else {
      visual_coord = 0;
    }
  } else {
    visual_coord += (Usz)scroll_offset;
  }
  if (visual_coord >= field_len)
    visual_coord = field_len - 1;
  return visual_coord;
}

void ged_mouse_event(Ged* a, Usz vis_y, Usz vis_x, mmask_t mouse_bstate) {
  if (mouse_bstate & BUTTON1_RELEASED) {
    // hard-disables tracking, but also disables further mouse stuff.
    // mousemask() with our original parameters seems to work to get into the
    // state we want, though.
    //
    // printf("\033[?1003l\n");
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
    a->is_mouse_down = false;
    a->is_mouse_dragging = false;
    a->drag_start_y = 0;
    a->drag_start_x = 0;
  } else if ((mouse_bstate & BUTTON1_PRESSED) || a->is_mouse_down) {
    Usz y = view_to_scrolled_grid(a->field.height, vis_y, a->grid_scroll_y);
    Usz x = view_to_scrolled_grid(a->field.width, vis_x, a->grid_scroll_x);
    if (!a->is_mouse_down) {
      // some sequence to hopefully make terminal start reporting all further
      // mouse movement events. 'REPORT_MOUSE_POSITION' alone in the mousemask
      // doesn't seem to work, at least not for xterm. we need to set it only
      // only when needed, otherwise some terminals will send movement updates
      // when we don't want them.
      printf("\033[?1003h\n");
      // need to do this or double clicking can cause terminal state to get
      // corrupted, since we're bypassing curses here. might cause flicker.
      // wish i could figure out why report mouse position isn't working on its
      // own.
      fflush(stdout);
      wclear(stdscr);
      a->is_mouse_down = true;
      a->ged_cursor.y = y;
      a->ged_cursor.x = x;
      a->ged_cursor.h = 1;
      a->ged_cursor.w = 1;
      a->is_draw_dirty = true;
    } else {
      if (!a->is_mouse_dragging &&
          (y != a->ged_cursor.y || x != a->ged_cursor.x)) {
        a->is_mouse_dragging = true;
        a->drag_start_y = a->ged_cursor.y;
        a->drag_start_x = a->ged_cursor.x;
      }
      if (a->is_mouse_dragging) {
        Usz tcy = a->drag_start_y;
        Usz tcx = a->drag_start_x;
        Usz loy = y < tcy ? y : tcy;
        Usz lox = x < tcx ? x : tcx;
        Usz hiy = y > tcy ? y : tcy;
        Usz hix = x > tcx ? x : tcx;
        a->ged_cursor.y = loy;
        a->ged_cursor.x = lox;
        a->ged_cursor.h = hiy - loy + 1;
        a->ged_cursor.w = hix - lox + 1;
        a->is_draw_dirty = true;
      }
    }
  }
#if defined(NCURSES_MOUSE_VERSION) && NCURSES_MOUSE_VERSION >= 2
  else {
    if (mouse_bstate & BUTTON4_PRESSED) {
      a->grid_scroll_y -= 1;
      a->is_draw_dirty = true;
    } else if (mouse_bstate & BUTTON5_PRESSED) {
      a->grid_scroll_y += 1;
      a->is_draw_dirty = true;
    }
  }
#endif
}

void ged_adjust_rulers_relative(Ged* a, Isz delta_y, Isz delta_x) {
  Isz new_y = (Isz)a->ruler_spacing_y + delta_y;
  Isz new_x = (Isz)a->ruler_spacing_x + delta_x;
  if (new_y < 4)
    new_y = 4;
  else if (new_y > 16)
    new_y = 16;
  if (new_x < 4)
    new_x = 4;
  else if (new_x > 16)
    new_x = 16;
  if ((Usz)new_y == a->ruler_spacing_y && (Usz)new_x == a->ruler_spacing_x)
    return;
  a->ruler_spacing_y = (Usz)new_y;
  a->ruler_spacing_x = (Usz)new_x;
  a->is_draw_dirty = true;
}

void ged_resize_grid_relative(Ged* a, Isz delta_y, Isz delta_x) {
  ged_resize_grid_snap_ruler(&a->field, &a->mbuf_r, a->ruler_spacing_y,
                             a->ruler_spacing_x, delta_y, delta_x, a->tick_num,
                             &a->scratch_field, &a->undo_hist, &a->ged_cursor);
  a->needs_remarking = true; // could check if we actually resized
  a->is_draw_dirty = true;
  ged_update_internal_geometry(a);
  ged_make_cursor_visible(a);
}

void ged_write_character(Ged* a, char c) {
  undo_history_push(&a->undo_hist, &a->field, a->tick_num);
  gbuffer_poke(a->field.buffer, a->field.height, a->field.width,
               a->ged_cursor.y, a->ged_cursor.x, c);
  // Indicate we want the next simulation step to be run predictavely,
  // so that we can use the reulsting mark buffer for UI visualization.
  // This is "expensive", so it could be skipped for non-interactive
  // input in situations where max throughput is necessary.
  a->needs_remarking = true;
  if (a->input_mode == Ged_input_mode_append) {
    ged_cursor_move_relative(&a->ged_cursor, a->field.height, a->field.width, 0,
                             1);
  }
  a->is_draw_dirty = true;
}

bool ged_fill_selection_with_char(Ged* a, Glyph c) {
  Usz curs_y, curs_x, curs_h, curs_w;
  if (!ged_try_selection_clipped_to_field(a, &curs_y, &curs_x, &curs_h,
                                          &curs_w))
    return false;
  gbuffer_fill_subrect(a->field.buffer, a->field.height, a->field.width, curs_y,
                       curs_x, curs_h, curs_w, c);
  return true;
}

bool ged_copy_selection_to_clipbard(Ged* a) {
  Usz curs_y, curs_x, curs_h, curs_w;
  if (!ged_try_selection_clipped_to_field(a, &curs_y, &curs_x, &curs_h,
                                          &curs_w))
    return false;
  Usz field_h = a->field.height;
  Usz field_w = a->field.width;
  Field* cb_field = &a->clipboard_field;
  field_resize_raw_if_necessary(cb_field, curs_h, curs_w);
  gbuffer_copy_subrect(a->field.buffer, cb_field->buffer, field_h, field_w,
                       curs_h, curs_w, curs_y, curs_x, 0, 0, curs_h, curs_w);
  return true;
}

void ged_input_character(Ged* a, char c) {
  switch (a->input_mode) {
  case Ged_input_mode_append:
    ged_write_character(a, c);
    break;
  case Ged_input_mode_normal:
  case Ged_input_mode_selresize:
  case Ged_input_mode_slide:
    if (a->ged_cursor.h <= 1 && a->ged_cursor.w <= 1) {
      ged_write_character(a, c);
    } else {
      undo_history_push(&a->undo_hist, &a->field, a->tick_num);
      ged_fill_selection_with_char(a, c);
      a->needs_remarking = true;
      a->is_draw_dirty = true;
    }
    break;
  }
}

typedef enum {
  Ged_input_cmd_undo,
  Ged_input_cmd_toggle_append_mode,
  Ged_input_cmd_toggle_selresize_mode,
  Ged_input_cmd_toggle_slide_mode,
  Ged_input_cmd_step_forward,
  Ged_input_cmd_toggle_show_event_list,
  Ged_input_cmd_toggle_play_pause,
  Ged_input_cmd_cut,
  Ged_input_cmd_copy,
  Ged_input_cmd_paste,
  Ged_input_cmd_escape,
} Ged_input_cmd;

void ged_input_cmd(Ged* a, Ged_input_cmd ev) {
  switch (ev) {
  case Ged_input_cmd_undo:
    if (undo_history_count(&a->undo_hist) > 0) {
      if (a->is_playing) {
        undo_history_apply(&a->undo_hist, &a->field, &a->tick_num);
      } else {
        undo_history_pop(&a->undo_hist, &a->field, &a->tick_num);
      }
      ged_cursor_confine(&a->ged_cursor, a->field.height, a->field.width);
      ged_update_internal_geometry(a);
      ged_make_cursor_visible(a);
      a->needs_remarking = true;
      a->is_draw_dirty = true;
    }
    break;
  case Ged_input_cmd_toggle_append_mode:
    if (a->input_mode == Ged_input_mode_append) {
      a->input_mode = Ged_input_mode_normal;
    } else {
      a->input_mode = Ged_input_mode_append;
    }
    a->is_draw_dirty = true;
    break;
  case Ged_input_cmd_toggle_selresize_mode:
    if (a->input_mode == Ged_input_mode_selresize) {
      a->input_mode = Ged_input_mode_normal;
    } else {
      a->input_mode = Ged_input_mode_selresize;
    }
    a->is_draw_dirty = true;
    break;
  case Ged_input_cmd_toggle_slide_mode:
    if (a->input_mode == Ged_input_mode_slide) {
      a->input_mode = Ged_input_mode_normal;
    } else {
      a->input_mode = Ged_input_mode_slide;
    }
    a->is_draw_dirty = true;
    break;
  case Ged_input_cmd_step_forward:
    undo_history_push(&a->undo_hist, &a->field, a->tick_num);
    clear_and_run_vm(a->field.buffer, a->mbuf_r.buffer, a->field.height,
                     a->field.width, a->tick_num, &a->oevent_list,
                     a->random_seed);
    ++a->tick_num;
    a->activity_counter += a->oevent_list.count;
    a->needs_remarking = true;
    a->is_draw_dirty = true;
    break;
  case Ged_input_cmd_toggle_play_pause:
    if (a->is_playing) {
      ged_stop_all_sustained_notes(a);
      a->is_playing = false;
      send_control_message(a->oosc_dev, "/orca/stopped");
    } else {
      undo_history_push(&a->undo_hist, &a->field, a->tick_num);
      a->is_playing = true;
      a->clock = stm_now();
      // dumb'n'dirty, get us close to the next step time, but not quite
      a->accum_secs = 60.0 / (double)a->bpm / 4.0 - 0.02;
      send_control_message(a->oosc_dev, "/orca/started");
    }
    a->is_draw_dirty = true;
    break;
  case Ged_input_cmd_toggle_show_event_list:
    a->draw_event_list = !a->draw_event_list;
    a->is_draw_dirty = true;
    break;
  case Ged_input_cmd_cut: {
    if (ged_copy_selection_to_clipbard(a)) {
      undo_history_push(&a->undo_hist, &a->field, a->tick_num);
      ged_fill_selection_with_char(a, '.');
      a->needs_remarking = true;
      a->is_draw_dirty = true;
    }
  } break;
  case Ged_input_cmd_copy: {
    ged_copy_selection_to_clipbard(a);
  } break;
  case Ged_input_cmd_paste: {
    Usz field_h = a->field.height;
    Usz field_w = a->field.width;
    Usz curs_y = a->ged_cursor.y;
    Usz curs_x = a->ged_cursor.x;
    if (curs_y >= field_h || curs_x >= field_w)
      break;
    Field* cb_field = &a->clipboard_field;
    Usz cbfield_h = cb_field->height;
    Usz cbfield_w = cb_field->width;
    Usz cpy_h = cbfield_h;
    Usz cpy_w = cbfield_w;
    if (field_h - curs_y < cpy_h)
      cpy_h = field_h - curs_y;
    if (field_w - curs_x < cpy_w)
      cpy_w = field_w - curs_x;
    if (cpy_h == 0 || cpy_w == 0)
      break;
    undo_history_push(&a->undo_hist, &a->field, a->tick_num);
    gbuffer_copy_subrect(cb_field->buffer, a->field.buffer, cbfield_h,
                         cbfield_w, field_h, field_w, 0, 0, curs_y, curs_x,
                         cpy_h, cpy_w);
    a->ged_cursor.h = cpy_h;
    a->ged_cursor.w = cpy_w;
    a->needs_remarking = true;
    a->is_draw_dirty = true;
  } break;
  case Ged_input_cmd_escape: {
    if (a->input_mode != Ged_input_mode_normal) {
      a->input_mode = Ged_input_mode_normal;
      a->is_draw_dirty = true;
    } else if (a->ged_cursor.h != 1 || a->ged_cursor.w != 1) {
      a->ged_cursor.h = 1;
      a->ged_cursor.w = 1;
      a->is_draw_dirty = true;
    }
  } break;
  }
}

bool hacky_try_save(Field* field, char const* filename) {
  if (!filename)
    return false;
  if (field->height == 0 || field->width == 0)
    return false;
  FILE* f = fopen(filename, "w");
  if (!f)
    return false;
  field_fput(field, f);
  fclose(f);
  return true;
}

//
// menu stuff
//

enum {
  Main_menu_id = 1,
  Open_form_id,
  Save_as_form_id,
  Set_tempo_form_id,
  Set_grid_dims_form_id,
  Autofit_menu_id,
  Confirm_new_file_menu_id,
#ifdef FEAT_PORTMIDI
  Portmidi_output_device_menu_id,
#endif
};
enum {
  Open_name_text_line_id = 1,
};
enum {
  Save_as_name_id = 1,
};
enum {
  Tempo_text_line_id = 1,
};
enum {
  Dims_text_line_id = 1,
};
enum {
  Autofit_nicely_id = 1,
  Autofit_tightly_id,
};
enum {
  Confirm_new_file_reject_id = 1,
  Confirm_new_file_accep_id,
};
enum {
  Main_menu_quit = 1,
  Main_menu_controls,
  Main_menu_opers_guide,
  Main_menu_new,
  Main_menu_open,
  Main_menu_save,
  Main_menu_save_as,
  Main_menu_set_tempo,
  Main_menu_set_grid_dims,
  Main_menu_autofit_grid,
  Main_menu_about,
#ifdef FEAT_PORTMIDI
  Main_menu_choose_portmidi_output,
#endif
};

void push_main_menu(void) {
  Qmenu* qm = qmenu_create(Main_menu_id);
  qmenu_set_title(qm, "ORCA");
  qmenu_add_choice(qm, Main_menu_new, "New");
  qmenu_add_choice(qm, Main_menu_open, "Open...");
  qmenu_add_choice(qm, Main_menu_save, "Save");
  qmenu_add_choice(qm, Main_menu_save_as, "Save As...");
  qmenu_add_spacer(qm);
  qmenu_add_choice(qm, Main_menu_set_tempo, "Set BPM...");
  qmenu_add_choice(qm, Main_menu_set_grid_dims, "Set Grid Size...");
  qmenu_add_choice(qm, Main_menu_autofit_grid, "Auto-fit Grid");
  qmenu_add_spacer(qm);
#ifdef FEAT_PORTMIDI
  qmenu_add_choice(qm, Main_menu_choose_portmidi_output, "MIDI Output...");
  qmenu_add_spacer(qm);
#endif
  qmenu_add_choice(qm, Main_menu_controls, "Controls...");
  qmenu_add_choice(qm, Main_menu_opers_guide, "Operators...");
  qmenu_add_choice(qm, Main_menu_about, "About...");
  qmenu_add_spacer(qm);
  qmenu_add_choice(qm, Main_menu_quit, "Quit");
  qmenu_push_to_nav(qm);
}

void pop_qnav_if_main_menu(void) {
  Qblock* qb = qnav_top_block();
  if (qb && qb->tag == Qblock_type_qmenu &&
      qmenu_id(qmenu_of(qb)) == Main_menu_id)
    qnav_stack_pop();
}

void push_confirm_new_file_menu(void) {
  Qmenu* qm = qmenu_create(Confirm_new_file_menu_id);
  qmenu_set_title(qm, "Are you sure?");
  qmenu_add_choice(qm, Confirm_new_file_reject_id, "Cancel");
  qmenu_add_choice(qm, Confirm_new_file_accep_id, "Create New File");
  qmenu_push_to_nav(qm);
}

void push_autofit_menu(void) {
  Qmenu* qm = qmenu_create(Autofit_menu_id);
  qmenu_set_title(qm, "Auto-fit Grid");
  qmenu_add_choice(qm, Autofit_nicely_id, "Nicely");
  qmenu_add_choice(qm, Autofit_tightly_id, "Tightly");
  qmenu_push_to_nav(qm);
}

void push_about_msg(void) {
  // clang-format off
  static char const* logo[] = {
  "lqqqk|lqqqk|lqqqk|lqqqk",
  "x   x|x   j|x    |lqqqu",
  "mqqqj|m    |mqqqj|m   j",
  };
  static char const* footer =
  "Live Programming Environment";
  // clang-format on
  int cols = (int)strlen(logo[0]);
  int hpad = 5;
  int tpad = 2;
  int bpad = 2;
  int sep = 1;
  int rows = (int)ORCA_ARRAY_COUNTOF(logo);
  int footer_len = (int)strlen(footer);
  int width = footer_len;
  if (cols > width)
    width = cols;
  width += hpad * 2;
  int logo_left_pad = (width - cols) / 2;
  int footer_left_pad = (width - footer_len) / 2;
  Qmsg* qm = qmsg_push(tpad + rows + sep + 1 + bpad, width);
  WINDOW* w = qmsg_window(qm);
  for (int row = 0; row < rows; ++row) {
    wmove(w, row + tpad, logo_left_pad);
    wattrset(w, A_BOLD);
    for (int col = 0; col < cols; ++col) {
      char c = logo[row][col];
      chtype ch;
      if (c == ' ')
        ch = (chtype)' ';
      else if (c == '|')
        ch = ACS_VLINE | (chtype)fg_bg(C_black, C_natural) | A_BOLD;
      else
        ch = NCURSES_ACS(c) | A_BOLD;
      waddch(w, ch);
    }
  }
  wattrset(w, A_DIM);
  wmove(w, tpad + rows + sep, footer_left_pad);
  waddstr(w, footer);
}

void push_controls_msg(void) {
  struct Ctrl_item {
    char const* input;
    char const* desc;
  };
  static struct Ctrl_item items[] = {
      {"Ctrl+Q", "Quit"},
      {"Arrow Keys", "Move Cursor"},
      {"Ctrl+D or F1", "Open Main Menu"},
      {"0-9, A-Z, a-z,", "Insert Character"},
      {"!, :, =, #, *", NULL},
      {"Spacebar", "Play/Pause"},
      {"Ctrl+Z or Ctrl+U", "Undo"},
      {"Ctrl+X", "Cut"},
      {"Ctrl+C", "Copy"},
      {"Ctrl+V", "Paste"},
      {"Ctrl+S", "Save"},
      {"Ctrl+F", "Frame Step Forward"},
      {"Ctrl+I or Insert", "Append/Overwrite Mode"},
      // {"/", "Key Trigger Mode"},
      {"' (quote)", "Rectangle Selection Mode"},
      {"Shift+Arrow Keys", "Adjust Rectangle Selection"},
      {"Alt+Arrow Keys", "Slide Selection"},
      {"` (grave) or ~", "Slide Selection Mode"},
      {"Escape", "Return to Normal Mode or Deselect"},
      {"( and )", "Resize Grid (Horizontal)"},
      {"_ and +", "Resize Grid (Vertical)"},
      {"[ and ]", "Adjust Grid Rulers (Horizontal)"},
      {"{ and }", "Adjust Grid Rulers (Vertical)"},
      {"< and >", "Adjust BPM"},
      {"?", "Controls (this message)"},
  };
  int w_input = 0;
  int w_desc = 0;
  for (Usz i = 0; i < ORCA_ARRAY_COUNTOF(items); ++i) {
    // use wcswidth instead of strlen if you need wide char support. but note
    // that won't be useful for UTF-8 or unicode chars in higher plane (emoji,
    // complex zwj, etc.)
    if (items[i].input) {
      int wl = (int)strlen(items[i].input);
      if (wl > w_input)
        w_input = wl;
    }
    if (items[i].desc) {
      int wr = (int)strlen(items[i].desc);
      if (wr > w_desc)
        w_desc = wr;
    }
  }
  int mid_pad = 2;
  int total_width = 1 + w_input + mid_pad + w_desc + 1;
  Qmsg* qm = qmsg_push(ORCA_ARRAY_COUNTOF(items), total_width);
  qmsg_set_title(qm, "Controls");
  WINDOW* w = qmsg_window(qm);
  for (int i = 0; i < (int)ORCA_ARRAY_COUNTOF(items); ++i) {
    if (items[i].input) {
      wmove(w, i, 1 + w_input - (int)strlen(items[i].input));
      waddstr(w, items[i].input);
    }
    if (items[i].desc) {
      wmove(w, i, 1 + w_input + mid_pad);
      waddstr(w, items[i].desc);
    }
  }
}

void push_opers_guide_msg(void) {
  struct Guide_item {
    char glyph;
    char const* name;
    char const* desc;
  };
  static struct Guide_item items[] = {
      {'A', "add", "Outputs sum of inputs."},
      {'B', "between", "Outputs subtraction of inputs."},
      {'C', "clock", "Outputs modulo of frame."},
      {'D', "delay", "Bangs on modulo of frame."},
      {'E', "east", "Moves eastward, or bangs."},
      {'F', "if", "Bangs if inputs are equal."},
      {'G', "generator", "Writes operands with offset."},
      {'H', "halt", "Halts southward operand."},
      {'I', "increment", "Increments southward operand."},
      {'J', "jumper", "Outputs northward operand."},
      {'K', "konkat", "Reads multiple variables."},
      {'L', "lesser", "Outputs smallest input."},
      {'M', "multiply", "Outputs product of inputs."},
      {'N', "north", "Moves Northward, or bangs."},
      {'O', "read", "Reads operand with offset."},
      {'P', "push", "Writes eastward operand."},
      {'Q', "query", "Reads operands with offset."},
      {'R', "random", "Outputs random value."},
      {'S', "south", "Moves southward, or bangs."},
      {'T', "track", "Reads eastward operand."},
      {'U', "uclid", "Bangs on Euclidean rhythm."},
      {'V', "variable", "Reads and writes variable."},
      {'W', "west", "Moves westward, or bangs."},
      {'X', "write", "Writes operand with offset."},
      {'Y', "jymper", "Outputs westward operand."},
      {'Z', "lerp", "Transitions operand to target."},
      {'*', "bang", "Bangs neighboring operands."},
      {'#', "comment", "Halts line."},
      // {'*', "self", "Sends ORCA command."},
      {':', "midi", "Sends MIDI note."},
      // {'!', "cc", "Sends MIDI control change."},
      // {'?', "pb", "Sends MIDI pitch bend."},
      // {'%', "mono", "Sends MIDI monophonic note."},
      {'=', "osc", "Sends OSC message."},
      {';', "udp", "Sends UDP message."},
  };
  int w_desc = 0;
  for (Usz i = 0; i < ORCA_ARRAY_COUNTOF(items); ++i) {
    if (items[i].desc) {
      int wr = (int)strlen(items[i].desc);
      if (wr > w_desc)
        w_desc = wr;
    }
  }
  int left_pad = 1;
  int mid_pad = 1;
  int right_pad = 1;
  int total_width = left_pad + 1 + mid_pad + w_desc + right_pad;
  Qmsg* qm = qmsg_push(ORCA_ARRAY_COUNTOF(items), total_width);
  qmsg_set_title(qm, "Operators");
  WINDOW* w = qmsg_window(qm);
  for (int i = 0; i < (int)ORCA_ARRAY_COUNTOF(items); ++i) {
    wmove(w, i, left_pad);
    waddch(w, (chtype)items[i].glyph | A_bold);
    wmove(w, i, left_pad + 1 + mid_pad);
    wattrset(w, A_normal);
    waddstr(w, items[i].desc);
  }
}

void push_open_form(char const* initial) {
  Qform* qf = qform_create(Open_form_id);
  qform_set_title(qf, "Open");
  qform_add_text_line(qf, Open_name_text_line_id, initial);
  qform_push_to_nav(qf);
}

bool try_save_with_msg(Field* field, sdd const* str) {
  if (!sdd_len(str))
    return false;
  bool ok = hacky_try_save(field, sddc(str));
  if (ok) {
    qmsg_printf_push(NULL, "Saved to:\n%s", sddc(str));
  } else {
    qmsg_printf_push("Error Saving File", "Unable to save file to:\n%s",
                     sddc(str));
  }
  return ok;
}

void push_save_as_form(char const* initial) {
  Qform* qf = qform_create(Save_as_form_id);
  qform_set_title(qf, "Save As");
  qform_add_text_line(qf, Save_as_name_id, initial);
  qform_push_to_nav(qf);
}

void push_set_tempo_form(Usz initial) {
  Qform* qf = qform_create(Set_tempo_form_id);
  char buff[64];
  int snres = snprintf(buff, sizeof buff, "%zu", initial);
  char const* inistr = snres > 0 && (Usz)snres < sizeof buff ? buff : "120";
  qform_set_title(qf, "Set BPM");
  qform_add_text_line(qf, Tempo_text_line_id, inistr);
  qform_push_to_nav(qf);
}

void push_set_grid_dims_form(Usz init_height, Usz init_width) {
  Qform* qf = qform_create(Set_grid_dims_form_id);
  char buff[128];
  int snres = snprintf(buff, sizeof buff, "%zux%zu", init_width, init_height);
  char const* inistr = snres > 0 && (Usz)snres < sizeof buff ? buff : "57x25";
  qform_set_title(qf, "Set Grid Size");
  qform_add_text_line(qf, Dims_text_line_id, inistr);
  qform_push_to_nav(qf);
}

#ifdef FEAT_PORTMIDI
void push_portmidi_output_device_menu(Midi_mode const* midi_mode) {
  Qmenu* qm = qmenu_create(Portmidi_output_device_menu_id);
  qmenu_set_title(qm, "PortMidi Device Selection");
  PmError e = portmidi_init_if_necessary();
  if (e) {
    qmenu_destroy(qm);
    qmsg_printf_push("PortMidi Error",
                     "PortMidi error during initialization:\n%s",
                     Pm_GetErrorText(e));
    return;
  }
  int num = Pm_CountDevices();
  int output_devices = 0;
  int cur_dev_id = 0;
  bool has_cur_dev_id = false;
  if (midi_mode->any.type == Midi_mode_type_portmidi) {
    cur_dev_id = midi_mode->portmidi.device_id;
    has_cur_dev_id = true;
  }
  for (int i = 0; i < num; ++i) {
    PmDeviceInfo const* info = Pm_GetDeviceInfo(i);
    if (!info || !info->output)
      continue;
    bool is_cur_dev_id = has_cur_dev_id && cur_dev_id == i;
    qmenu_add_printf(qm, i, "[%c] #%d - %s", is_cur_dev_id ? '*' : ' ', i,
                     info->name);
    ++output_devices;
  }
  if (output_devices == 0) {
    qmenu_destroy(qm);
    qmsg_printf_push("No PortMidi Devices",
                     "No PortMidi output devices found.");
    return;
  }
  if (has_cur_dev_id) {
    qmenu_set_current_item(qm, cur_dev_id);
  }
  qmenu_push_to_nav(qm);
}
#endif

//
// Misc utils
//

bool read_int(char const* str, int* out) {
  int a;
  int res = sscanf(str, "%d", &a);
  if (res != 1)
    return false;
  *out = a;
  return true;
}

// Reads something like '5x3' or '5'. Writes the same value to both outputs if
// only one is specified. Returns false on error.
bool read_nxn_or_n(char const* str, int* out_a, int* out_b) {
  int a, b;
  int res = sscanf(str, "%dx%d", &a, &b);
  if (res == EOF)
    return false;
  if (res == 1) {
    *out_a = a;
    *out_b = a;
    return true;
  }
  if (res == 2) {
    *out_a = a;
    *out_b = b;
    return true;
  }
  return false;
}

typedef enum {
  Bracketed_paste_sequence_none = 0,
  Bracketed_paste_sequence_begin,
  Bracketed_paste_sequence_end,
} Bracketed_paste_sequence;

Bracketed_paste_sequence bracketed_paste_sequence_getch_ungetch(WINDOW* win) {
  int esc1 = wgetch(win);
  if (esc1 == '[') {
    int esc2 = wgetch(win);
    if (esc2 == '2') {
      int esc3 = wgetch(win);
      if (esc3 == '0') {
        int esc4 = wgetch(win);
        // Start or end of bracketed paste
        if (esc4 == '0' || esc4 == '1') {
          int esc5 = wgetch(win);
          if (esc5 == '~') {
            switch (esc4) {
            case '0':
              return Bracketed_paste_sequence_begin;
            case '1':
              return Bracketed_paste_sequence_end;
            }
          }
          ungetch(esc5);
        }
        ungetch(esc4);
      }
      ungetch(esc3);
    }
    ungetch(esc2);
  }
  ungetch(esc1);
  return Bracketed_paste_sequence_none;
}

void try_send_to_gui_clipboard(Ged const* a, bool* io_use_gui_clipboard) {
  if (!*io_use_gui_clipboard)
    return;
#if 0 // If we want to use grid directly
  Usz curs_y, curs_x, curs_h, curs_w;
  if (!ged_try_selection_clipped_to_field(a, &curs_y, &curs_x, &curs_h,
                                          &curs_w))
    return;
  Cboard_error cberr =
      cboard_copy(a->clipboard_field.buffer, a->clipboard_field.height,
                  a->clipboard_field.width, curs_y, curs_x, curs_h, curs_w);
#endif
  Usz cb_h = a->clipboard_field.height, cb_w = a->clipboard_field.width;
  if (cb_h < 1 || cb_w < 1)
    return;
  Cboard_error cberr =
      cboard_copy(a->clipboard_field.buffer, cb_h, cb_w, 0, 0, cb_h, cb_w);
  if (cberr) {
    *io_use_gui_clipboard = false;
    switch (cberr) {
    case Cboard_error_none:
    case Cboard_error_unavailable:
    case Cboard_error_popen_failed:
    case Cboard_error_process_exit_error:
      break;
    }
  }
}

char const* field_load_error_string(Field_load_error fle) {
  char const* errstr = "Unknown";
  switch (fle) {
  case Field_load_error_ok:
    errstr = "OK";
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
  return errstr;
}

//
// main
//

enum {
  Argopt_margins = UCHAR_MAX + 1,
  Argopt_hardmargins,
  Argopt_undo_limit,
  Argopt_init_grid_size,
  Argopt_osc_server,
  Argopt_osc_port,
  Argopt_osc_midi_bidule,
  Argopt_strict_timing,
  Argopt_bpm,
  Argopt_seed,
#ifdef FEAT_PORTMIDI
  Argopt_portmidi_list_devices,
  Argopt_portmidi_output_device,
#endif
};

int main(int argc, char** argv) {
  static struct option tui_options[] = {
      {"margins", required_argument, 0, Argopt_margins},
      {"hard-margins", required_argument, 0, Argopt_hardmargins},
      {"undo-limit", required_argument, 0, Argopt_undo_limit},
      {"initial-size", required_argument, 0, Argopt_init_grid_size},
      {"help", no_argument, 0, 'h'},
      {"osc-server", required_argument, 0, Argopt_osc_server},
      {"osc-port", required_argument, 0, Argopt_osc_port},
      {"osc-midi-bidule", required_argument, 0, Argopt_osc_midi_bidule},
      {"strict-timing", no_argument, 0, Argopt_strict_timing},
      {"bpm", required_argument, 0, Argopt_bpm},
      {"seed", required_argument, 0, Argopt_seed},
#ifdef FEAT_PORTMIDI
      {"portmidi-list-devices", no_argument, 0, Argopt_portmidi_list_devices},
      {"portmidi-output-device", required_argument, 0,
       Argopt_portmidi_output_device},
#endif
      {NULL, 0, NULL, 0}};
  sdd* file_name = NULL;
  int undo_history_limit = 100;
  char const* osc_hostname = NULL;
  char const* osc_port = NULL;
  bool strict_timing = false;
  int init_bpm = 120;
  int init_seed = 1;
  bool should_autosize_grid = true;
  int init_grid_dim_y = 25;
  int init_grid_dim_x = 57;
  bool use_gui_cboard = true;
  Midi_mode midi_mode;
  midi_mode_init_null(&midi_mode);

  int softmargin_y = 1;
  int softmargin_x = 2;
  int hardmargin_y = 0;
  int hardmargin_x = 0;

  for (;;) {
    int c = getopt_long(argc, argv, "h", tui_options, NULL);
    if (c == -1)
      break;
    switch (c) {
    case 'h':
      usage();
      exit(0);
    case '?':
      usage();
      exit(1);
    case Argopt_margins: {
      bool ok = read_nxn_or_n(optarg, &softmargin_x, &softmargin_y) &&
                softmargin_x >= 0 && softmargin_y >= 0;
      if (!ok) {
        fprintf(stderr,
                "Bad margins argument %s.\n"
                "Must be 0 or positive integer.\n",
                optarg);
        exit(1);
      }
    } break;
    case Argopt_hardmargins: {
      bool ok = read_nxn_or_n(optarg, &hardmargin_x, &hardmargin_y) &&
                hardmargin_x >= 0 && hardmargin_y >= 0;
      if (!ok) {
        fprintf(stderr,
                "Bad hard-margins argument %s.\n"
                "Must be 0 or positive integer.\n",
                optarg);
        exit(1);
      }
    } break;
    case Argopt_undo_limit: {
      if (!read_int(optarg, &undo_history_limit) || undo_history_limit < 0) {
        fprintf(stderr,
                "Bad undo-limit argument %s.\n"
                "Must be 0 or positive integer.\n",
                optarg);
        exit(1);
      }
    } break;
    case Argopt_bpm: {
      init_bpm = atoi(optarg);
      if (init_bpm < 1) {
        fprintf(stderr,
                "Bad bpm argument %s.\n"
                "Must be positive integer.\n",
                optarg);
        exit(1);
      }
    } break;
    case Argopt_seed: {
      if (!read_int(optarg, &init_seed) || init_seed < 0) {
        fprintf(stderr,
                "Bad seed argument %s.\n"
                "Must be 0 or positive integer.\n",
                optarg);
        exit(1);
      }
    } break;
    case Argopt_init_grid_size: {
      should_autosize_grid = false;
      enum {
        Max_dim_arg_val_y = ORCA_Y_MAX,
        Max_dim_arg_val_x = ORCA_X_MAX,
      };
      if (sscanf(optarg, "%dx%d", &init_grid_dim_x, &init_grid_dim_y) != 2) {
        fprintf(stderr, "Bad argument format or count for initial-size.\n");
        exit(1);
      }
      if (init_grid_dim_x <= 0 || init_grid_dim_x > Max_dim_arg_val_x) {
        fprintf(stderr,
                "X dimension for initial-size must be 1 <= n <= %d, was %d.\n",
                Max_dim_arg_val_x, init_grid_dim_x);
        exit(1);
      }
      if (init_grid_dim_y <= 0 || init_grid_dim_y > Max_dim_arg_val_y) {
        fprintf(stderr,
                "Y dimension for initial-size must be 1 <= n <= %d, was %d.\n",
                Max_dim_arg_val_y, init_grid_dim_y);
        exit(1);
      }
    } break;
    case Argopt_osc_server: {
      osc_hostname = optarg;
    } break;
    case Argopt_osc_port: {
      osc_port = optarg;
    } break;
    case Argopt_osc_midi_bidule: {
      midi_mode_deinit(&midi_mode);
      midi_mode_init_osc_bidule(&midi_mode, optarg);
    } break;
    case Argopt_strict_timing: {
      strict_timing = true;
    } break;
#ifdef FEAT_PORTMIDI
    case Argopt_portmidi_list_devices: {
      Pm_Initialize();
      int num = Pm_CountDevices();
      int output_devices = 0;
      for (int i = 0; i < num; ++i) {
        PmDeviceInfo const* info = Pm_GetDeviceInfo(i);
        if (!info || !info->output)
          continue;
        printf("ID: %-4d Name: %s\n", i, info->name);
        ++output_devices;
      }
      if (output_devices == 0) {
        printf("No PortMidi output devices detected.\n");
      }
      Pm_Terminate();
      exit(0);
    }
    case Argopt_portmidi_output_device: {
      int dev_id;
      if (!read_int(optarg, &dev_id) || dev_id < 0) {
        fprintf(stderr,
                "Bad portmidi-output-device argument %s.\n"
                "Must be 0 or positive integer.\n",
                optarg);
        exit(1);
      }
      midi_mode_deinit(&midi_mode);
      PmError pme = midi_mode_init_portmidi(&midi_mode, dev_id);
      if (pme) {
        fprintf(stderr, "PortMidi error: %s\n", Pm_GetErrorText(pme));
        exit(1);
      }
      // todo a bunch of places where we don't terminate pm on exit. Guess we
      // should make a wrapper.
    }
#endif
    }
  }

  if (optind == argc - 1) {
    should_autosize_grid = false;
    file_name = sdd_new(argv[optind]);
  } else if (optind < argc - 1) {
    fprintf(stderr, "Expected only 1 file argument.\n");
    exit(1);
  }

  qnav_init();
  Ged ged_state;
  ged_init(&ged_state, (Usz)undo_history_limit, (Usz)init_bpm, (Usz)init_seed);

  if (osc_hostname != NULL && osc_port == NULL) {
    fprintf(stderr,
            "An OSC server address was specified, but no OSC port was "
            "specified.\n"
            "OSC output is not possible without specifying an OSC port.\n");
    ged_deinit(&ged_state);
    exit(1);
  }
  if (midi_mode.any.type == Midi_mode_type_osc_bidule && osc_port == NULL) {
    fprintf(stderr,
            "MIDI was set to be sent via OSC formatted for Plogue Bidule,\n"
            "but no OSC port was specified.\n"
            "OSC output is not possible without specifying an OSC port.\n");
    ged_deinit(&ged_state);
    exit(1);
  }
  if (osc_port != NULL) {
    if (!ged_set_osc_udp(&ged_state, osc_hostname, osc_port)) {
      fprintf(stderr, "Failed to set up OSC networking\n");
      ged_deinit(&ged_state);
      exit(1);
    }
  }

  if (file_name) {
    Field_load_error fle = field_load_file(sddc(file_name), &ged_state.field);
    if (fle != Field_load_error_ok) {
      char const* errstr = field_load_error_string(fle);
      fprintf(stderr, "File load error: %s.\n", errstr);
      ged_deinit(&ged_state);
      qnav_deinit();
      sdd_free(file_name);
      exit(1);
    }
  } else {
    file_name = sdd_newcap(0);
    // Temp hacky stuff: we've crammed two code paths into the KEY_RESIZE event
    // case. One of them is for the initial setup for an automatic grid size.
    // The other is for actual resize events. We will factor this out into
    // procedures in the future, but until then, we've made a slight mess. In
    // the case where the user has explicitly specified a size, we'll allocate
    // the Field stuff here. If there's an automatic size, then we'll allocate
    // the field in the KEY_RESIZE handler. The reason we don't just allocate
    // it here and then again later is to avoid an extra allocation and memory
    // manipulation.
    if (!should_autosize_grid) {
      field_init_fill(&ged_state.field, (Usz)init_grid_dim_y,
                      (Usz)init_grid_dim_x, '.');
    }
  }
  ged_state.filename = sdd_len(file_name) ? sddc(file_name) : "unnamed";
  ged_set_midi_mode(&ged_state, &midi_mode);

  // Set up timer lib
  stm_setup();

  // Enable UTF-8 by explicitly initializing our locale before initializing
  // ncurses. Only needed (maybe?) if using libncursesw/wide-chars or UTF-8.
  // Using it unguarded will mess up box drawing chars in Linux virtual
  // consoles unless using libncursesw.
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
  // Receive keyboard input immediately without line buffering, and receive
  // ctrl+z, ctrl+c etc. as input instead of having a signal generated. We need
  // to do this even with wtimeout() if we don't want ctrl+z etc. to interrupt
  // the program.
  raw();
  // Don't echo keyboard input
  noecho();
  // Also receive arrow keys, etc.
  keypad(stdscr, TRUE);
  // Hide the terminal cursor
  curs_set(0);
  // Short delay before triggering escape
  set_escdelay(1);
  // Our color init routine
  term_util_init_colors();

  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
  if (has_mouse()) {
    // no waiting for distinguishing click from press
    mouseinterval(0);
  }

  printf("\033[?2004h\n"); // Ask terminal to use bracketed paste.

  WINDOW* cont_window = NULL;

  int key = KEY_RESIZE;
  wtimeout(stdscr, 0);
  int cur_timeout = 0;
  Usz bracketed_paste_starting_x = 0, bracketed_paste_y = 0,
      bracketed_paste_x = 0, bracketed_paste_max_y = 0,
      bracketed_paste_max_x = 0;
  bool is_in_bracketed_paste = false;

  // Send initial BPM
  send_num_message(ged_state.oosc_dev, "/orca/bpm", (I32)ged_state.bpm);

  for (;;) {
    switch (key) {
    case ERR: {
      ged_do_stuff(&ged_state);
      bool drew_any = false;
      if (qnav_stack.stack_changed)
        drew_any = true;
      if (ged_is_draw_dirty(&ged_state) || drew_any) {
        werase(cont_window);
        ged_draw(&ged_state, cont_window);
        wnoutrefresh(cont_window);
        drew_any = true;
      }
      int term_h, term_w;
      if (qnav_stack.count > 0) // todo lame, move this
        getmaxyx(stdscr, term_h, term_w);
      for (Usz i = 0; i < qnav_stack.count; ++i) {
        Qblock* qb = qnav_stack.blocks[i];
        if (qnav_stack.stack_changed) {
          bool is_frontmost = i == qnav_stack.count - 1;
          qblock_print_frame(qb, is_frontmost);
          switch (qb->tag) {
          case Qblock_type_qmsg:
            break;
          case Qblock_type_qmenu: {
            Qmenu* qm = qmenu_of(qb);
            qmenu_set_displayed_active(qm, is_frontmost);
          } break;
          case Qblock_type_qform:
            break;
          }
        }
        touchwin(qb->outer_window); // here? or after continue?
        if (term_h < 1 || term_w < 1)
          continue;
        int qbwin_h, qbwin_w;
        getmaxyx(qb->outer_window, qbwin_h, qbwin_w);
        int qbwin_endy = qb->y + qbwin_h;
        int qbwin_endx = qb->x + qbwin_w;
        if (qbwin_endy >= term_h)
          qbwin_endy = term_h - 1;
        if (qbwin_endx >= term_w)
          qbwin_endx = term_w - 1;
        if (qb->y >= qbwin_endy || qb->x >= qbwin_endx)
          continue;
        pnoutrefresh(qb->outer_window, 0, 0, qb->y, qb->x, qbwin_endy,
                     qbwin_endx);
        drew_any = true;
      }
      qnav_stack.stack_changed = false;
      if (drew_any)
        doupdate();
      double secs_to_d = ged_secs_to_deadline(&ged_state);
      int new_timeout;
      // These values are tuned to work OK with the normal scheduling behavior
      // on Linux, Mac, and Windows. Of course, there's no guarantee about how
      // the scheduler will work so if you are using a modified kernel or
      // something, this might be sub-optimal. But there's not really much we
      // can do about it!
      if (strict_timing) {
        if (secs_to_d < ms_to_sec(0.5)) {
          new_timeout = 0;
        } else if (secs_to_d < ms_to_sec(1.5)) {
          new_timeout = 0;
        } else if (secs_to_d < ms_to_sec(3.0)) {
          new_timeout = 1;
        } else if (secs_to_d < ms_to_sec(5.0)) {
          new_timeout = 2;
        } else if (secs_to_d < ms_to_sec(7.0)) {
          new_timeout = 3;
        } else if (secs_to_d < ms_to_sec(9.0)) {
          new_timeout = 4;
        } else if (secs_to_d < ms_to_sec(11.0)) {
          new_timeout = 5;
        } else if (secs_to_d < ms_to_sec(13.0)) {
          new_timeout = 6;
        } else if (secs_to_d < ms_to_sec(15.0)) {
          new_timeout = 7;
        } else if (secs_to_d < ms_to_sec(25.0)) {
          new_timeout = 12;
        } else if (secs_to_d < ms_to_sec(50.0)) {
          new_timeout = 20;
        } else if (secs_to_d < ms_to_sec(100.0)) {
          new_timeout = 40;
        } else {
          new_timeout = 50;
        }
      } else {
        if (secs_to_d < ms_to_sec(0.5)) {
          new_timeout = 0;
        } else if (secs_to_d < ms_to_sec(1.0)) {
          new_timeout = 0;
        } else if (secs_to_d < ms_to_sec(2.0)) {
          new_timeout = 1;
        } else if (secs_to_d < ms_to_sec(7.0)) {
          new_timeout = 2;
        } else if (secs_to_d < ms_to_sec(15.0)) {
          new_timeout = 5;
        } else if (secs_to_d < ms_to_sec(25.0)) {
          new_timeout = 10;
        } else if (secs_to_d < ms_to_sec(50.0)) {
          new_timeout = 20;
        } else if (secs_to_d < ms_to_sec(100.0)) {
          new_timeout = 40;
        } else {
          new_timeout = 50;
        }
      }
      if (new_timeout != cur_timeout) {
        wtimeout(stdscr, new_timeout);
        cur_timeout = new_timeout;
#if TIME_DEBUG
        spin_track_timeout = cur_timeout;
#endif
      }
      goto next_getch;
    }
    case KEY_RESIZE: {
      int term_h, term_w;
      getmaxyx(stdscr, term_h, term_w);
      assert(term_h >= 0 && term_w >= 0);
      int content_y = 0, content_x = 0;
      int content_h = term_h, content_w = term_w;
      if (hardmargin_y > 0 && term_h > hardmargin_y * 2 + 2) {
        content_y += hardmargin_y;
        content_h -= hardmargin_y * 2;
      }
      if (hardmargin_x > 0 && term_w > hardmargin_x * 2 + 2) {
        content_x += hardmargin_x;
        content_w -= hardmargin_x * 2;
      }
      bool remake_window = true;
      if (cont_window) {
        int cwin_y, cwin_x, cwin_h, cwin_w;
        getbegyx(cont_window, cwin_y, cwin_x);
        getmaxyx(cont_window, cwin_h, cwin_w);
        remake_window = cwin_y != content_y || cwin_x != content_x ||
                        cwin_h != content_h || cwin_w != content_w;
      }
      if (remake_window) {
        if (cont_window) {
          delwin(cont_window);
        }
        wclear(stdscr);
        cont_window =
            derwin(stdscr, content_h, content_w, content_y, content_x);
        ged_state.is_draw_dirty = true;
      }
      // We might do this once soon after startup if the user specified neither
      // a starting grid size or a file to open. See above (search KEY_RESIZE)
      // for why this is kind of messy and hacky -- we'll be changing this
      // again before too long, so we haven't made too much of an attempt to
      // keep it non-messy.
      if (should_autosize_grid) {
        should_autosize_grid = false;
        Usz new_field_h, new_field_w;
        if (ged_suggest_nice_grid_size(
                content_h, content_w, softmargin_y, softmargin_x,
                (int)ged_state.ruler_spacing_y, (int)ged_state.ruler_spacing_x,
                &new_field_h, &new_field_w)) {
          field_init_fill(&ged_state.field, (Usz)new_field_h, (Usz)new_field_w,
                          '.');
          mbuf_reusable_ensure_size(&ged_state.mbuf_r, new_field_h,
                                    new_field_w);
          ged_make_cursor_visible(&ged_state);
        } else {
          field_init_fill(&ged_state.field, (Usz)init_grid_dim_y,
                          (Usz)init_grid_dim_x, '.');
        }
      }
      // OK to call this unconditionally -- deriving the sub-window areas is
      // more than a single comparison, and we don't want to split up or
      // duplicate the math and checks for it, so this routine will calculate
      // the stuff it needs to and then early-out if there's no further work.
      ged_set_window_size(&ged_state, content_h, content_w, softmargin_y,
                          softmargin_x);
      goto next_getch;
    }
#ifndef FEAT_NOMOUSE
    case KEY_MOUSE: {
      MEVENT mevent;
      if (cont_window && getmouse(&mevent) == OK) {
        int win_y, win_x;
        int win_h, win_w;
        getbegyx(cont_window, win_y, win_x);
        getmaxyx(cont_window, win_h, win_w);
        int inwin_y = mevent.y - win_y;
        int inwin_x = mevent.x - win_x;
        if (inwin_y >= win_h)
          inwin_y = win_h - 1;
        if (inwin_y < 0)
          inwin_y = 0;
        if (inwin_x >= win_w)
          inwin_x = win_w - 1;
        if (inwin_x < 0)
          inwin_x = 0;
        ged_mouse_event(&ged_state, (Usz)inwin_y, (Usz)inwin_x, mevent.bstate);
      }
      goto next_getch;
    }
#endif
    }

    Qblock* qb = qnav_top_block();
    if (qb) {
      if (key == CTRL_PLUS('q'))
        goto quit;
      switch (qb->tag) {
      case Qblock_type_qmsg: {
        Qmsg* qm = qmsg_of(qb);
        if (qmsg_drive(qm, key))
          qnav_stack_pop();
      } break;
      case Qblock_type_qmenu: {
        Qmenu* qm = qmenu_of(qb);
        Qmenu_action act;
        // special case for main menu: pressing the key to open it will close
        // it again.
        if (qmenu_id(qm) == Main_menu_id &&
            (key == CTRL_PLUS('d') || key == KEY_F(1))) {
          qnav_stack_pop();
          break;
        }
        if (qmenu_drive(qm, key, &act)) {
          switch (act.any.type) {
          case Qmenu_action_type_canceled: {
            qnav_stack_pop();
          } break;
          case Qmenu_action_type_picked: {
            switch (qmenu_id(qm)) {
            case Main_menu_id: {
              switch (act.picked.id) {
              case Main_menu_quit:
                goto quit;
              case Main_menu_controls:
                push_controls_msg();
                break;
              case Main_menu_opers_guide:
                push_opers_guide_msg();
                break;
              case Main_menu_about:
                push_about_msg();
                break;
              case Main_menu_new:
                push_confirm_new_file_menu();
                break;
              case Main_menu_open:
                push_open_form(sddc(file_name));
                break;
              case Main_menu_save:
                if (sdd_len(file_name) > 0) {
                  try_save_with_msg(&ged_state.field, file_name);
                } else {
                  push_save_as_form("");
                }
                break;
              case Main_menu_save_as:
                push_save_as_form(sddc(file_name));
                break;
              case Main_menu_set_tempo:
                push_set_tempo_form(ged_state.bpm);
                break;
              case Main_menu_set_grid_dims:
                push_set_grid_dims_form(ged_state.field.height,
                                        ged_state.field.width);
                break;
              case Main_menu_autofit_grid:
                push_autofit_menu();
                break;
#ifdef FEAT_PORTMIDI
              case Main_menu_choose_portmidi_output:
                push_portmidi_output_device_menu(&midi_mode);
                break;
#endif
              }
            } break;
            case Autofit_menu_id: {
              Usz new_field_h, new_field_w;
              bool did_get_ok_size = false;
              switch (act.picked.id) {
              case Autofit_nicely_id:
                did_get_ok_size = ged_suggest_nice_grid_size(
                    ged_state.win_h, ged_state.win_w, ged_state.softmargin_y,
                    ged_state.softmargin_x, (int)ged_state.ruler_spacing_y,
                    (int)ged_state.ruler_spacing_x, &new_field_h, &new_field_w);
                break;
              case Autofit_tightly_id:
                did_get_ok_size = ged_suggest_tight_grid_size(
                    ged_state.win_h, ged_state.win_w, ged_state.softmargin_y,
                    ged_state.softmargin_x, &new_field_h, &new_field_w);
                break;
              }
              if (did_get_ok_size) {
                ged_resize_grid(&ged_state.field, &ged_state.mbuf_r,
                                new_field_h, new_field_w, ged_state.tick_num,
                                &ged_state.scratch_field, &ged_state.undo_hist,
                                &ged_state.ged_cursor);
                ged_update_internal_geometry(&ged_state);
                ged_state.needs_remarking = true;
                ged_state.is_draw_dirty = true;
                ged_make_cursor_visible(&ged_state);
              }
              qnav_stack_pop();
              pop_qnav_if_main_menu();
            } break;
            case Confirm_new_file_menu_id: {
              switch (act.picked.id) {
              case Confirm_new_file_reject_id:
                qnav_stack_pop();
                break;
              case Confirm_new_file_accep_id: {
                Usz new_field_h, new_field_w;
                if (ged_suggest_nice_grid_size(ged_state.win_h, ged_state.win_w,
                                               ged_state.softmargin_y,
                                               ged_state.softmargin_x,
                                               (int)ged_state.ruler_spacing_y,
                                               (int)ged_state.ruler_spacing_x,
                                               &new_field_h, &new_field_w)) {
                  undo_history_push(&ged_state.undo_hist, &ged_state.field,
                                    ged_state.tick_num);
                  field_resize_raw(&ged_state.field, new_field_h, new_field_w);
                  memset(ged_state.field.buffer, '.',
                         new_field_h * new_field_w * sizeof(Glyph));
                  ged_cursor_confine(&ged_state.ged_cursor, new_field_h,
                                     new_field_w);
                  mbuf_reusable_ensure_size(&ged_state.mbuf_r, new_field_h,
                                            new_field_w);
                  ged_update_internal_geometry(&ged_state);
                  ged_make_cursor_visible(&ged_state);
                  ged_state.needs_remarking = true;
                  ged_state.is_draw_dirty = true;
                  sdd_clear(file_name);
                  ged_state.filename = "unnamed"; // slightly redundant
                  qnav_stack_pop();
                  pop_qnav_if_main_menu();
                }
              } break;
              }
            } break;
#ifdef FEAT_PORTMIDI
            case Portmidi_output_device_menu_id: {
              midi_mode_deinit(&midi_mode);
              PmError pme = midi_mode_init_portmidi(&midi_mode, act.picked.id);
              qnav_stack_pop();
              if (pme) {
                qmsg_printf_push("PortMidi Error",
                                 "Error setting PortMidi output device:\n%s",
                                 Pm_GetErrorText(pme));
              }
            } break;
#endif
            }
          } break;
          }
        }
      } break;
      case Qblock_type_qform: {
        Qform* qf = qform_of(qb);
        Qform_action act;
        if (qform_drive(qf, key, &act)) {
          switch (act.any.type) {
          case Qform_action_type_canceled:
            qnav_stack_pop();
            break;
          case Qform_action_type_submitted: {
            switch (qform_id(qf)) {
            case Open_form_id: {
              sdd* temp_name = NULL;
              if (qform_get_text_line(qf, Open_name_text_line_id, &temp_name) &&
                  sdd_len(temp_name) > 0) {
                undo_history_push(&ged_state.undo_hist, &ged_state.field,
                                  ged_state.tick_num);
                Field_load_error fle =
                    field_load_file(sddc(temp_name), &ged_state.field);
                if (fle == Field_load_error_ok) {
                  qnav_stack_pop();
                  file_name = sdd_cpysdd(file_name, temp_name);
                  ged_state.filename = sddc(file_name);
                  mbuf_reusable_ensure_size(&ged_state.mbuf_r,
                                            ged_state.field.height,
                                            ged_state.field.width);
                  ged_cursor_confine(&ged_state.ged_cursor,
                                     ged_state.field.height,
                                     ged_state.field.width);
                  ged_update_internal_geometry(&ged_state);
                  ged_make_cursor_visible(&ged_state);
                  ged_state.needs_remarking = true;
                  ged_state.is_draw_dirty = true;
                  pop_qnav_if_main_menu();
                } else {
                  undo_history_pop(&ged_state.undo_hist, &ged_state.field,
                                   &ged_state.tick_num);
                  qmsg_printf_push("Error Loading File", "%s:\n%s",
                                   sddc(temp_name),
                                   field_load_error_string(fle));
                }
              }
              sdd_free(temp_name);
            } break;
            case Save_as_form_id: {
              sdd* temp_name = NULL;
              if (qform_get_text_line(qf, Save_as_name_id, &temp_name) &&
                  sdd_len(temp_name) > 0) {
                qnav_stack_pop();
                bool saved_ok = try_save_with_msg(&ged_state.field, temp_name);
                if (saved_ok) {
                  file_name = sdd_cpysdd(file_name, temp_name);
                  ged_state.filename = sddc(file_name);
                }
              }
              sdd_free(temp_name);
            } break;
            case Set_tempo_form_id: {
              sdd* tmpstr = NULL;
              if (qform_get_text_line(qf, Tempo_text_line_id, &tmpstr) &&
                  sdd_len(tmpstr) > 0) {
                int newbpm = atoi(sddc(tmpstr));
                if (newbpm > 0) {
                  ged_state.bpm = (Usz)newbpm;
                  qnav_stack_pop();
                }
              }
              sdd_free(tmpstr);
            } break;
            case Set_grid_dims_form_id: {
              sdd* tmpstr = NULL;
              if (qform_get_text_line(qf, Tempo_text_line_id, &tmpstr) &&
                  sdd_len(tmpstr) > 0) {
                int newheight, newwidth;
                if (sscanf(sddc(tmpstr), "%dx%d", &newwidth, &newheight) == 2 &&
                    newheight > 0 && newwidth > 0 && newheight < ORCA_Y_MAX &&
                    newwidth < ORCA_X_MAX) {
                  if (ged_state.field.height != (Usz)newheight ||
                      ged_state.field.width != (Usz)newwidth) {
                    ged_resize_grid(
                        &ged_state.field, &ged_state.mbuf_r, (Usz)newheight,
                        (Usz)newwidth, ged_state.tick_num,
                        &ged_state.scratch_field, &ged_state.undo_hist,
                        &ged_state.ged_cursor);
                    ged_update_internal_geometry(&ged_state);
                    ged_state.needs_remarking = true;
                    ged_state.is_draw_dirty = true;
                    ged_make_cursor_visible(&ged_state);
                  }
                  qnav_stack_pop();
                }
              }
              sdd_free(tmpstr);
            } break;
            }
          } break;
          }
        }
      } break;
      }
      goto next_getch;
    }

    // If this key input is intended to reach the grid, check to see if we're
    // in bracketed paste and use alternate 'filtered input for characters'
    // mode. We'll ignore most control sequences here.
    if (is_in_bracketed_paste) {
      if (key == 27 /* escape */) {
        if (bracketed_paste_sequence_getch_ungetch(stdscr) ==
            Bracketed_paste_sequence_end) {
          is_in_bracketed_paste = false;
          if (bracketed_paste_max_y > ged_state.ged_cursor.y)
            ged_state.ged_cursor.h =
                bracketed_paste_max_y - ged_state.ged_cursor.y + 1;
          if (bracketed_paste_max_x > ged_state.ged_cursor.x)
            ged_state.ged_cursor.w =
                bracketed_paste_max_x - ged_state.ged_cursor.x + 1;
          ged_state.needs_remarking = true;
          ged_state.is_draw_dirty = true;
        }
        goto next_getch;
      }
      if (key == KEY_ENTER)
        key = '\r';
      if (key >= CHAR_MIN && key <= CHAR_MAX) {
        if ((char)key == '\r' || (char)key == '\n') {
          bracketed_paste_x = bracketed_paste_starting_x;
          ++bracketed_paste_y;
          goto next_getch;
        }
        if (key != ' ') {
          char cleaned = (char)key;
          if (!is_valid_glyph((Glyph)key))
            cleaned = '.';
          if (bracketed_paste_y < ged_state.field.height &&
              bracketed_paste_x < ged_state.field.width) {
            gbuffer_poke(ged_state.field.buffer, ged_state.field.height,
                         ged_state.field.width, bracketed_paste_y,
                         bracketed_paste_x, cleaned);
            // Could move this out one level if we wanted the final selection
            // size to reflect even the pasted area which didn't fit on the
            // grid.
            if (bracketed_paste_y > bracketed_paste_max_y)
              bracketed_paste_max_y = bracketed_paste_y;
            if (bracketed_paste_x > bracketed_paste_max_x)
              bracketed_paste_max_x = bracketed_paste_x;
          }
        }
        ++bracketed_paste_x;
      }
      goto next_getch;
    }

    // Regular inputs when we're not in a menu and not in bracketed paste.
    switch (key) {
    // Checking again for 'quit' here, because it's only listened for if we're
    // in the menus or *not* in bracketed paste mode.
    case CTRL_PLUS('q'):
      goto quit;
    case CTRL_PLUS('o'):
      push_open_form(sddc(file_name));
      break;
    case KEY_UP:
    case CTRL_PLUS('k'):
      ged_dir_input(&ged_state, Ged_dir_up, 1);
      break;
    case CTRL_PLUS('j'):
    case KEY_DOWN:
      ged_dir_input(&ged_state, Ged_dir_down, 1);
      break;
    case 127: // backspace in terminal.app, apparently
    case KEY_BACKSPACE:
      if (ged_state.input_mode == Ged_input_mode_append) {
        ged_dir_input(&ged_state, Ged_dir_left, 1);
        ged_input_character(&ged_state, '.');
        ged_dir_input(&ged_state, Ged_dir_left, 1);
      } else {
        ged_input_character(&ged_state, '.');
      }
      break;
    case CTRL_PLUS('h'):
    case KEY_LEFT:
      ged_dir_input(&ged_state, Ged_dir_left, 1);
      break;
    case CTRL_PLUS('l'):
    case KEY_RIGHT:
      ged_dir_input(&ged_state, Ged_dir_right, 1);
      break;
    case CTRL_PLUS('z'):
    case CTRL_PLUS('u'):
      ged_input_cmd(&ged_state, Ged_input_cmd_undo);
      break;
    case '[':
      ged_adjust_rulers_relative(&ged_state, 0, -1);
      break;
    case ']':
      ged_adjust_rulers_relative(&ged_state, 0, 1);
      break;
    case '{':
      ged_adjust_rulers_relative(&ged_state, -1, 0);
      break;
    case '}':
      ged_adjust_rulers_relative(&ged_state, 1, 0);
      break;
    case '(':
      ged_resize_grid_relative(&ged_state, 0, -1);
      break;
    case ')':
      ged_resize_grid_relative(&ged_state, 0, 1);
      break;
    case '_':
      ged_resize_grid_relative(&ged_state, -1, 0);
      break;
    case '+':
      ged_resize_grid_relative(&ged_state, 1, 0);
      break;
    case '\r':
    case KEY_ENTER:
      // Currently unused. Formerly was the toggle for insert/append mode.
      break;
    case CTRL_PLUS('i'):
    case KEY_IC:
      ged_input_cmd(&ged_state, Ged_input_cmd_toggle_append_mode);
      break;
    case '/':
      // Currently unused. Formerly 'piano'/trigger mode toggle.
      break;
    case '<':
      ged_adjust_bpm(&ged_state, -1);
      break;
    case '>':
      ged_adjust_bpm(&ged_state, 1);
      break;
    case CTRL_PLUS('f'):
      ged_input_cmd(&ged_state, Ged_input_cmd_step_forward);
      break;
    case CTRL_PLUS('e'):
      ged_input_cmd(&ged_state, Ged_input_cmd_toggle_show_event_list);
      break;
    case CTRL_PLUS('x'):
      ged_input_cmd(&ged_state, Ged_input_cmd_cut);
      try_send_to_gui_clipboard(&ged_state, &use_gui_cboard);
      break;
    case CTRL_PLUS('c'):
      ged_input_cmd(&ged_state, Ged_input_cmd_copy);
      try_send_to_gui_clipboard(&ged_state, &use_gui_cboard);
      break;
    case CTRL_PLUS('v'):
      if (use_gui_cboard) {
        undo_history_push(&ged_state.undo_hist, &ged_state.field,
                          ged_state.tick_num);
        Usz pasted_h, pasted_w;
        Cboard_error cberr =
            cboard_paste(ged_state.field.buffer, ged_state.field.height,
                         ged_state.field.width, ged_state.ged_cursor.y,
                         ged_state.ged_cursor.x, &pasted_h, &pasted_w);
        if (cberr) {
          undo_history_pop(&ged_state.undo_hist, &ged_state.field,
                           &ged_state.tick_num);
          switch (cberr) {
          case Cboard_error_none:
            break;
          case Cboard_error_unavailable:
          case Cboard_error_popen_failed:
          case Cboard_error_process_exit_error:
            break;
          }
          use_gui_cboard = false;
          ged_input_cmd(&ged_state, Ged_input_cmd_paste);
        } else {
          if (pasted_h > 0 && pasted_w > 0) {
            ged_state.ged_cursor.h = pasted_h;
            ged_state.ged_cursor.w = pasted_w;
          }
        }
        ged_state.needs_remarking = true;
        ged_state.is_draw_dirty = true;
      } else {
        ged_input_cmd(&ged_state, Ged_input_cmd_paste);
      }
      break;
    case '\'':
      ged_input_cmd(&ged_state, Ged_input_cmd_toggle_selresize_mode);
      break;
    case '`':
    case '~':
      ged_input_cmd(&ged_state, Ged_input_cmd_toggle_slide_mode);
      break;
    case ' ':
      if (ged_state.input_mode == Ged_input_mode_append) {
        ged_input_character(&ged_state, '.');
      } else {
        ged_input_cmd(&ged_state, Ged_input_cmd_toggle_play_pause);
      }
      break;
    case 27: { // Escape
      // Check for escape sequences we're interested in that ncurses didn't
      // handle.
      if (bracketed_paste_sequence_getch_ungetch(stdscr) ==
          Bracketed_paste_sequence_begin) {
        is_in_bracketed_paste = true;
        undo_history_push(&ged_state.undo_hist, &ged_state.field,
                          ged_state.tick_num);
        bracketed_paste_y = ged_state.ged_cursor.y;
        bracketed_paste_x = ged_state.ged_cursor.x;
        bracketed_paste_starting_x = bracketed_paste_x;
        bracketed_paste_max_y = bracketed_paste_y;
        bracketed_paste_max_x = bracketed_paste_x;
        break;
      }
      ged_input_cmd(&ged_state, Ged_input_cmd_escape);
    } break;

    // Selection size modification. These may not work in all terminals. (Only
    // tested in xterm so far.)
    case 337: // shift-up
      ged_modify_selection_size(&ged_state, -1, 0);
      break;
    case 336: // shift-down
      ged_modify_selection_size(&ged_state, 1, 0);
      break;
    case 393: // shift-left
      ged_modify_selection_size(&ged_state, 0, -1);
      break;
    case 402: // shift-right
      ged_modify_selection_size(&ged_state, 0, 1);
      break;
    case 567: // shift-control-up
      ged_modify_selection_size(&ged_state, -(int)ged_state.ruler_spacing_y, 0);
      break;
    case 526: // shift-control-down
      ged_modify_selection_size(&ged_state, (int)ged_state.ruler_spacing_y, 0);
      break;
    case 546: // shift-control-left
      ged_modify_selection_size(&ged_state, 0, -(int)ged_state.ruler_spacing_x);
      break;
    case 561: // shift-control-right
      ged_modify_selection_size(&ged_state, 0, (int)ged_state.ruler_spacing_x);
      break;

    case 330: // delete?
      ged_input_character(&ged_state, '.');
      break;

    // Jump on control-arrow
    case 566: // control-up
      ged_dir_input(&ged_state, Ged_dir_up, (int)ged_state.ruler_spacing_y);
      break;
    case 525: // control-down
      ged_dir_input(&ged_state, Ged_dir_down, (int)ged_state.ruler_spacing_y);
      break;
    case 545: // control-left
      ged_dir_input(&ged_state, Ged_dir_left, (int)ged_state.ruler_spacing_x);
      break;
    case 560: // control-right
      ged_dir_input(&ged_state, Ged_dir_right, (int)ged_state.ruler_spacing_x);
      break;

    // Slide selection on alt-arrow
    case 564: // alt-up
      ged_slide_selection(&ged_state, -1, 0);
      break;
    case 523: // alt-down
      ged_slide_selection(&ged_state, 1, 0);
      break;
    case 543: // alt-left
      ged_slide_selection(&ged_state, 0, -1);
      break;
    case 558: // alt-right
      ged_slide_selection(&ged_state, 0, 1);
      break;

    case CTRL_PLUS('d'):
    case KEY_F(1):
      push_main_menu();
      break;
    case '?':
      push_controls_msg();
      break;
    case CTRL_PLUS('g'):
      push_opers_guide_msg();
      break;
    case CTRL_PLUS('s'):
      // TODO duplicated with menu item code
      if (sdd_len(file_name) > 0) {
        try_save_with_msg(&ged_state.field, file_name);
      } else {
        push_save_as_form("");
      }
      break;

    default:
      if (key >= CHAR_MIN && key <= CHAR_MAX && is_valid_glyph((Glyph)key)) {
        ged_input_character(&ged_state, (char)key);
      }
#if 0
      else {
        fprintf(stderr, "Unknown key number: %d\n", key);
      }
#endif
      break;
    }
  next_getch:
    key = wgetch(stdscr);
    if (cur_timeout != 0) {
      wtimeout(stdscr, 0);
      cur_timeout = 0;
    }
  }
quit:
  ged_stop_all_sustained_notes(&ged_state);
  qnav_deinit();
  if (cont_window) {
    delwin(cont_window);
  }
  printf("\033[?2004h\n"); // Tell terminal to not use bracketed paste
  endwin();
  ged_deinit(&ged_state);
  sdd_free(file_name);
  midi_mode_deinit(&midi_mode);
#ifdef FEAT_PORTMIDI
  if (portmidi_is_initialized)
    Pm_Terminate();
#endif
  return 0;
}
