#include "bank.h"
#include "base.h"
#include "field.h"
#include "gbuffer.h"
#include "mark.h"
#include "osc_out.h"
#include "sim.h"
#include <getopt.h>
#include <locale.h>
#include <ncurses.h>

#define SOKOL_IMPL
#include "sokol_time.h"
#undef SOKOL_IMPL

#define CTRL_PLUS(c) ((c)&037)

static void usage() {
  // clang-format off
  fprintf(stderr,
      "Usage: orca [options] [file]\n\n"
      "General options:\n"
      "    --margins <number> Set cosmetic margins.\n"
      "                       Default: 2\n"
      "    -h or --help       Print this message and exit.\n"
      "\n"
      "OSC/MIDI options:\n"
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
      );
  // clang-format on
}

typedef enum {
  Tui_input_mode_normal = 0,
  Tui_input_mode_append = 1,
  Tui_input_mode_piano = 2,
  Tui_input_mode_selresize = 3,
} Tui_input_mode;

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
  case '!':
  case ':':
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

static bool is_valid_glyph(Glyph c) {
  if (c >= '0' && c <= '9')
    return true;
  if (c >= 'A' && c <= 'Z')
    return true;
  if (c >= 'a' && c <= 'z')
    return true;
  switch (c) {
  case '!':
  case '.':
  case '*':
  case ':':
  case ';':
  case '#':
    return true;
  }
  return false;
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
  Usz h;
  Usz w;
} Tui_cursor;

void tui_cursor_init(Tui_cursor* tc) {
  tc->y = 0;
  tc->x = 0;
  tc->h = 1;
  tc->w = 1;
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

void tdraw_grid_cursor(WINDOW* win, int draw_y, int draw_x, int draw_h,
                       int draw_w, Glyph const* gbuffer, Usz field_h,
                       Usz field_w, int scroll_y, int scroll_x, Usz cursor_y,
                       Usz cursor_x, Usz cursor_h, Usz cursor_w,
                       Tui_input_mode input_mode, bool is_playing) {
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
  int const curs_attr = A_reverse | A_bold | fg_bg(C_yellow, C_natural);
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
      chtype ch = (chtype)(displayed | curs_attr);
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
      chbuffer[ix] = (chtype)((int)(chbuffer[ix] & A_CHARTEXT) | curs_attr);
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

void undo_history_apply(Undo_history* hist, Field* out_field,
                        Usz* out_tick_num) {
  Undo_node* last = hist->last;
  if (!last)
    return;
  field_copy(&last->field, out_field);
  *out_tick_num = last->tick_num;
}

Usz undo_history_count(Undo_history* hist) { return hist->count; }

void tdraw_hud(WINDOW* win, int win_y, int win_x, int height, int width,
               const char* filename, Usz field_h, Usz field_w,
               Usz ruler_spacing_y, Usz ruler_spacing_x, Usz tick_num, Usz bpm,
               Tui_cursor* const tui_cursor, Tui_input_mode input_mode) {
  (void)height;
  (void)width;
  wmove(win, win_y, win_x);
  wprintw(win, "%dx%d\t%d/%d\t%df\t%d\t-------", (int)field_w, (int)field_h,
          (int)ruler_spacing_x, (int)ruler_spacing_y, (int)tick_num, (int)bpm);
  wclrtoeol(win);
  wmove(win, win_y + 1, win_x);
  wprintw(win, "%d,%d\t%d:%d\tcell\t", (int)tui_cursor->x, (int)tui_cursor->y,
          (int)tui_cursor->w, (int)tui_cursor->h);
  switch (input_mode) {
  case Tui_input_mode_normal:
    wattrset(win, A_normal);
    wprintw(win, "insert");
    break;
  case Tui_input_mode_append:
    wattrset(win, A_bold);
    wprintw(win, "append");
    break;
  case Tui_input_mode_piano:
    wattrset(win, A_reverse);
    wprintw(win, "trigger");
    break;
  case Tui_input_mode_selresize:
    wattrset(win, A_bold);
    wprintw(win, "select");
    break;
  }
  wattrset(win, A_normal);
  wprintw(win, "\t%s", filename);
  wclrtoeol(win);
}

void tdraw_glyphs_grid(WINDOW* win, int draw_y, int draw_x, int draw_h,
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
  if (rows == 0 || cols == 0)
    return;
  bool use_rulers = ruler_spacing_y != 0 && ruler_spacing_x != 0;
  (void)use_rulers;
  for (Usz iy = 0; iy < rows; ++iy) {
    Usz line_offset = (offset_y + iy) * field_w + offset_x;
    Glyph const* g_row = gbuffer + line_offset;
    Mark const* m_row = mbuffer + line_offset;
    bool use_y_ruler = use_rulers && (iy + offset_y) % ruler_spacing_y == 0;
    for (Usz ix = 0; ix < cols; ++ix) {
      Glyph g = g_row[ix];
      Mark m = m_row[ix];
      int attrs = term_attrs_of_cell(g, m);
      if (g == '.') {
        if (use_y_ruler && (ix + offset_x) % ruler_spacing_x == 0)
          g = '+';
      }
      chbuffer[ix] = (chtype)((int)g | attrs);
    }
    wmove(win, draw_y + (int)iy, draw_x);
    waddchnstr(win, chbuffer, (int)cols);
  }
}

void tdraw_glyphs_grid_scrolled(WINDOW* win, int draw_y, int draw_x, int draw_h,
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
  tdraw_glyphs_grid(win, draw_y, draw_x, draw_h, draw_w, gbuffer, mbuffer,
                    field_h, field_w, (Usz)scroll_y, (Usz)scroll_x,
                    ruler_spacing_y, ruler_spacing_x);
}

void tui_cursor_confine(Tui_cursor* tc, Usz height, Usz width) {
  if (height == 0 || width == 0)
    return;
  if (tc->y >= height)
    tc->y = height - 1;
  if (tc->x >= width)
    tc->x = width - 1;
}

void tdraw_oevent_list(WINDOW* win, Oevent_list const* oevent_list) {
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
      Oevent_midi const* em = (Oevent_midi const*)ev;
      wprintw(win,
              "MIDI\tchannel %d\toctave %d\tnote %d\tvelocity %d\tlength %d",
              (int)em->channel, (int)em->octave, (int)em->note,
              (int)em->velocity, (int)em->bar_divisor);
      break;
    }
    }
  }
}

void tui_resize_grid(Field* field, Markmap_reusable* markmap, Usz new_height,
                     Usz new_width, Usz tick_num, Field* scratch_field,
                     Undo_history* undo_hist, Tui_cursor* tui_cursor) {
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
  tui_cursor_confine(tui_cursor, new_height, new_width);
  markmap_reusable_ensure_size(markmap, new_height, new_width);
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
bool tui_resize_grid_snap_ruler(Field* field, Markmap_reusable* markmap,
                                Usz ruler_y, Usz ruler_x, Isz delta_h,
                                Isz delta_w, Usz tick_num, Field* scratch_field,
                                Undo_history* undo_hist,
                                Tui_cursor* tui_cursor) {
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
  if (new_field_h == field_h && new_field_w == field_w)
    return false;
  tui_resize_grid(field, markmap, new_field_h, new_field_w, tick_num,
                  scratch_field, undo_hist, tui_cursor);
  return true;
}

typedef enum {
  Midi_mode_type_null,
  Midi_mode_type_osc_bidule,
} Midi_mode_type;

typedef struct {
  Midi_mode_type type;
} Midi_mode_any;

typedef struct {
  Midi_mode_type type;
  char const* path;
} Midi_mode_osc_bidule;

typedef union {
  Midi_mode_any any;
  Midi_mode_osc_bidule osc_bidule;
} Midi_mode;

void midi_mode_init(Midi_mode* mm) { mm->any.type = Midi_mode_type_null; }
void midi_mode_set_osc_bidule(Midi_mode* mm, char const* path) {
  mm->osc_bidule.type = Midi_mode_type_osc_bidule;
  mm->osc_bidule.path = path;
}

typedef struct {
  Field field;
  Field scratch_field;
  Field clipboard_field;
  Markmap_reusable markmap_r;
  Bank bank;
  Undo_history undo_hist;
  Oevent_list oevent_list;
  Oevent_list scratch_oevent_list;
  Susnote_list susnote_list;
  Tui_cursor tui_cursor;
  Piano_bits piano_bits;
  Usz tick_num;
  Usz ruler_spacing_y, ruler_spacing_x;
  Tui_input_mode input_mode;
  Usz bpm;
  double accum_secs;
  double time_to_next_note_off;
  char const* filename;
  Oosc_dev* oosc_dev;
  Midi_mode const* midi_mode;
  Usz drag_start_y;
  Usz drag_start_x;
  int win_h;
  int win_w;
  int grid_h;
  int grid_scroll_y; // not sure if i like this being int
  int grid_scroll_x;
  bool needs_remarking : 1;
  bool is_draw_dirty : 1;
  bool is_playing : 1;
  bool draw_event_list : 1;
  bool is_mouse_down : 1;
  bool is_mouse_dragging : 1;
  bool is_hud_visible : 1;
} App_state;

void app_init(App_state* a) {
  field_init(&a->field);
  field_init(&a->scratch_field);
  field_init(&a->clipboard_field);
  markmap_reusable_init(&a->markmap_r);
  bank_init(&a->bank);
  undo_history_init(&a->undo_hist);
  tui_cursor_init(&a->tui_cursor);
  oevent_list_init(&a->oevent_list);
  oevent_list_init(&a->scratch_oevent_list);
  susnote_list_init(&a->susnote_list);
  a->piano_bits = ORCA_PIANO_BITS_NONE;
  a->tick_num = 0;
  a->ruler_spacing_y = 8;
  a->ruler_spacing_x = 8;
  a->input_mode = Tui_input_mode_normal;
  a->bpm = 120;
  a->accum_secs = 0.0;
  a->time_to_next_note_off = 1.0;
  a->filename = NULL;
  a->oosc_dev = NULL;
  a->midi_mode = NULL;
  a->win_h = 0;
  a->win_w = 0;
  a->grid_h = 0;
  a->grid_scroll_y = 0;
  a->grid_scroll_x = 0;
  a->drag_start_y = 0;
  a->drag_start_x = 0;
  a->needs_remarking = true;
  a->is_draw_dirty = false;
  a->is_playing = false;
  a->draw_event_list = false;
  a->is_mouse_down = false;
  a->is_mouse_dragging = false;
  a->is_hud_visible = false;
}

void app_deinit(App_state* a) {
  field_deinit(&a->field);
  field_deinit(&a->scratch_field);
  field_deinit(&a->clipboard_field);
  markmap_reusable_deinit(&a->markmap_r);
  bank_deinit(&a->bank);
  undo_history_deinit(&a->undo_hist);
  oevent_list_deinit(&a->oevent_list);
  oevent_list_deinit(&a->scratch_oevent_list);
  susnote_list_deinit(&a->susnote_list);
  if (a->oosc_dev) {
    oosc_dev_destroy(a->oosc_dev);
  }
}

bool app_is_draw_dirty(App_state* a) {
  return a->is_draw_dirty || a->needs_remarking;
}

bool app_set_osc_udp(App_state* a, char const* dest_addr,
                     char const* dest_port) {
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

void app_set_midi_mode(App_state* a, Midi_mode const* midi_mode) {
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
      I32 ints[3];
      ints[0] = (0x8 << 4) | (U8)chan; // status
      ints[1] = (I32)note;             // note number
      ints[2] = 0;                     // velocity
      oosc_send_int32s(oosc_dev, midi_mode->osc_bidule.path, ints,
                       ORCA_ARRAY_COUNTOF(ints));
    } break;
    }
  }
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

void app_stop_all_sustained_notes(App_state* a) {
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
  double bar_secs = (double)bpm / 60.0;

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
      Oevent_midi const* em = (Oevent_midi const*)&e->midi;
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
    }
  }

  if (midi_note_count == 0)
    return;

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
      I32 ints[3];
      ints[0] = (0x9 << 4) | mno.channel; // status
      ints[1] = mno.note_number;          // note number
      ints[2] = mno.velocity;             // velocity
      oosc_send_int32s(oosc_dev, midi_mode->osc_bidule.path, ints,
                       ORCA_ARRAY_COUNTOF(ints));
    } break;
    }
  }
}

double app_secs_to_deadline(App_state const* a) {
  if (a->is_playing) {
    double secs_span = 60.0 / (double)a->bpm / 4.0;
    double rem = secs_span - a->accum_secs;
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

void app_apply_delta_secs(App_state* a, double secs) {
  if (a->is_playing) {
    a->accum_secs += secs;
    Oosc_dev* oosc_dev = a->oosc_dev;
    Midi_mode const* midi_mode = a->midi_mode;
    apply_time_to_sustained_notes(oosc_dev, midi_mode, secs, &a->susnote_list,
                                  &a->time_to_next_note_off);
  }
}

void app_do_stuff(App_state* a) {
  double secs_span = 60.0 / (double)a->bpm / 4.0;
  Oosc_dev* oosc_dev = a->oosc_dev;
  Midi_mode const* midi_mode = a->midi_mode;
  // Clamp to 1 second of buffered play time, in case the process get frozen,
  // we don't want to play back a ton of steps all at once.
  if (a->accum_secs > 1.0)
    a->accum_secs = 1.0;
  while (a->accum_secs > secs_span) {
    a->accum_secs -= secs_span;
    orca_run(a->field.buffer, a->markmap_r.buffer, a->field.height,
             a->field.width, a->tick_num, &a->bank, &a->oevent_list,
             a->piano_bits);
    ++a->tick_num;
    a->piano_bits = ORCA_PIANO_BITS_NONE;
    a->needs_remarking = true;
    a->is_draw_dirty = true;

    if (oosc_dev && midi_mode) {
      Usz count = a->oevent_list.count;
      if (count > 0) {
        send_output_events(oosc_dev, midi_mode, a->bpm, &a->susnote_list,
                           a->oevent_list.buffer, count);
      }
    }
    // note for future: sustained note deadlines may have changed due to note
    // on. will need to update stored deadline in memory if
    // app_apply_delta_secs isn't called again immediately after app_do_stuff.
  }
}

static double ms_to_sec(double ms) { return ms / 1000.0; }

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

void app_make_cursor_visible(App_state* a) {
  int grid_h = a->grid_h;
  int cur_scr_y = a->grid_scroll_y;
  int cur_scr_x = a->grid_scroll_x;
  int new_scr_y = (int)scroll_offset_on_axis_for_cursor_pos(
      grid_h, (Isz)a->field.height, (Isz)a->tui_cursor.y, 5, cur_scr_y);
  int new_scr_x = (int)scroll_offset_on_axis_for_cursor_pos(
      a->win_w, (Isz)a->field.width, (Isz)a->tui_cursor.x, 5, cur_scr_x);
  if (new_scr_y == cur_scr_y && new_scr_x == cur_scr_x)
    return;
  a->grid_scroll_y = new_scr_y;
  a->grid_scroll_x = new_scr_x;
  a->is_draw_dirty = true;
}

enum { Hud_height = 2 };

void app_set_window_size(App_state* a, int win_h, int win_w) {
  bool draw_hud = win_h > Hud_height + 1;
  int grid_h = draw_hud ? win_h - 2 : win_h;
  a->win_h = win_h;
  a->win_w = win_w;
  a->grid_h = grid_h;
  a->is_draw_dirty = true;
  a->is_hud_visible = draw_hud;
  app_make_cursor_visible(a);
}

void app_draw(App_state* a, WINDOW* win) {
  werase(win);
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
  if (a->needs_remarking) {
    field_resize_raw_if_necessary(&a->scratch_field, a->field.height,
                                  a->field.width);
    field_copy(&a->field, &a->scratch_field);
    markmap_reusable_ensure_size(&a->markmap_r, a->field.height,
                                 a->field.width);
    orca_run(a->scratch_field.buffer, a->markmap_r.buffer, a->field.height,
             a->field.width, a->tick_num, &a->bank, &a->scratch_oevent_list,
             a->piano_bits);
    a->needs_remarking = false;
  }
  int win_h = a->win_h;
  int win_w = a->win_w;
  tdraw_glyphs_grid_scrolled(win, 0, 0, a->grid_h, win_w, a->field.buffer,
                             a->markmap_r.buffer, a->field.height,
                             a->field.width, a->grid_scroll_y, a->grid_scroll_x,
                             a->ruler_spacing_y, a->ruler_spacing_x);
  tdraw_grid_cursor(win, 0, 0, a->grid_h, win_w, a->field.buffer,
                    a->field.height, a->field.width, a->grid_scroll_y,
                    a->grid_scroll_x, a->tui_cursor.y, a->tui_cursor.x,
                    a->tui_cursor.h, a->tui_cursor.w, a->input_mode,
                    a->is_playing);
  if (a->is_hud_visible) {
    char const* filename = a->filename ? a->filename : "";
    tdraw_hud(win, win_h - Hud_height, 0, Hud_height, win_w, filename,
              a->field.height, a->field.width, a->ruler_spacing_y,
              a->ruler_spacing_x, a->tick_num, a->bpm, &a->tui_cursor,
              a->input_mode);
  }
  if (a->draw_event_list) {
    tdraw_oevent_list(win, &a->oevent_list);
  }
  a->is_draw_dirty = false;
  wrefresh(win);
}

void app_adjust_bpm(App_state* a, Isz delta_bpm) {
  Isz new_bpm = (Isz)a->bpm + delta_bpm;
  if (new_bpm < 1)
    new_bpm = 1;
  else if (new_bpm > 3000)
    new_bpm = 3000;
  if ((Usz)new_bpm != a->bpm) {
    a->bpm = (Usz)new_bpm;
    a->is_draw_dirty = true;
  }
}

void app_move_cursor_relative(App_state* a, Isz delta_y, Isz delta_x) {
  tui_cursor_move_relative(&a->tui_cursor, a->field.height, a->field.width,
                           delta_y, delta_x);
  app_make_cursor_visible(a);
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

void app_modify_selection_size(App_state* a, int delta_y, int delta_x) {
  Usz cur_h = a->tui_cursor.h;
  Usz cur_w = a->tui_cursor.w;
  Usz new_h = guarded_selection_axis_resize(cur_h, delta_y);
  Usz new_w = guarded_selection_axis_resize(cur_w, delta_x);
  if (cur_h != new_h || cur_w != new_w) {
    a->tui_cursor.h = new_h;
    a->tui_cursor.w = new_w;
    a->is_draw_dirty = true;
  }
}

typedef enum {
  App_dir_up,
  App_dir_down,
  App_dir_left,
  App_dir_right,
} App_dir;

void app_dir_input(App_state* a, App_dir dir) {
  switch (a->input_mode) {
  case Tui_input_mode_normal:
  case Tui_input_mode_append:
  case Tui_input_mode_piano:
    switch (dir) {
    case App_dir_up:
      app_move_cursor_relative(a, -1, 0);
      break;
    case App_dir_down:
      app_move_cursor_relative(a, 1, 0);
      break;
    case App_dir_left:
      app_move_cursor_relative(a, 0, -1);
      break;
    case App_dir_right:
      app_move_cursor_relative(a, 0, 1);
      break;
    }
    break;
  case Tui_input_mode_selresize:
    switch (dir) {
    case App_dir_up:
      app_modify_selection_size(a, -1, 0);
      break;
    case App_dir_down:
      app_modify_selection_size(a, 1, 0);
      break;
    case App_dir_left:
      app_modify_selection_size(a, 0, -1);
      break;
    case App_dir_right:
      app_modify_selection_size(a, 0, 1);
      break;
    }
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

void app_mouse_event(App_state* a, Usz vis_y, Usz vis_x, mmask_t mouse_bstate) {
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
      a->tui_cursor.y = y;
      a->tui_cursor.x = x;
      a->tui_cursor.h = 1;
      a->tui_cursor.w = 1;
      a->is_draw_dirty = true;
    } else {
      if (!a->is_mouse_dragging &&
          (y != a->tui_cursor.y || x != a->tui_cursor.x)) {
        a->is_mouse_dragging = true;
        a->drag_start_y = a->tui_cursor.y;
        a->drag_start_x = a->tui_cursor.x;
      }
      if (a->is_mouse_dragging) {
        Usz tcy = a->drag_start_y;
        Usz tcx = a->drag_start_x;
        Usz loy = y < tcy ? y : tcy;
        Usz lox = x < tcx ? x : tcx;
        Usz hiy = y > tcy ? y : tcy;
        Usz hix = x > tcx ? x : tcx;
        a->tui_cursor.y = loy;
        a->tui_cursor.x = lox;
        a->tui_cursor.h = hiy - loy + 1;
        a->tui_cursor.w = hix - lox + 1;
        a->is_draw_dirty = true;
      }
    }
  } else {
    if (mouse_bstate & BUTTON4_PRESSED) {
      a->grid_scroll_y -= 1;
      a->is_draw_dirty = true;
    } else if (mouse_bstate & BUTTON5_PRESSED) {
      a->grid_scroll_y += 1;
      a->is_draw_dirty = true;
    }
  }
}

void app_adjust_rulers_relative(App_state* a, Isz delta_y, Isz delta_x) {
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

void app_resize_grid_relative(App_state* a, Isz delta_y, Isz delta_x) {
  tui_resize_grid_snap_ruler(&a->field, &a->markmap_r, a->ruler_spacing_y,
                             a->ruler_spacing_x, delta_y, delta_x, a->tick_num,
                             &a->scratch_field, &a->undo_hist, &a->tui_cursor);
  a->needs_remarking = true; // could check if we actually resized
  a->is_draw_dirty = true;
  app_make_cursor_visible(a);
}

void app_write_character(App_state* a, char c) {
  undo_history_push(&a->undo_hist, &a->field, a->tick_num);
  gbuffer_poke(a->field.buffer, a->field.height, a->field.width,
               a->tui_cursor.y, a->tui_cursor.x, c);
  // Indicate we want the next simulation step to be run predictavely,
  // so that we can use the reulsting mark buffer for UI visualization.
  // This is "expensive", so it could be skipped for non-interactive
  // input in situations where max throughput is necessary.
  a->needs_remarking = true;
  if (a->input_mode == Tui_input_mode_append) {
    tui_cursor_move_relative(&a->tui_cursor, a->field.height, a->field.width, 0,
                             1);
  }
  a->is_draw_dirty = true;
}

void app_add_piano_bits_for_character(App_state* a, char c) {
  Piano_bits added_bits = piano_bits_of((Glyph)c);
  a->piano_bits |= added_bits;
}

bool app_try_selection_clipped_to_field(App_state const* a, Usz* out_y,
                                        Usz* out_x, Usz* out_h, Usz* out_w) {
  Usz curs_y = a->tui_cursor.y;
  Usz curs_x = a->tui_cursor.x;
  Usz curs_h = a->tui_cursor.h;
  Usz curs_w = a->tui_cursor.w;
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

bool app_fill_selection_with_char(App_state* a, Glyph c) {
  Usz curs_y, curs_x, curs_h, curs_w;
  if (!app_try_selection_clipped_to_field(a, &curs_y, &curs_x, &curs_h,
                                          &curs_w))
    return false;
  gbuffer_fill_subrect(a->field.buffer, a->field.height, a->field.width, curs_y,
                       curs_x, curs_h, curs_w, c);
  return true;
}

bool app_copy_selection_to_clipbard(App_state* a) {
  Usz curs_y, curs_x, curs_h, curs_w;
  if (!app_try_selection_clipped_to_field(a, &curs_y, &curs_x, &curs_h,
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

void app_input_character(App_state* a, char c) {
  switch (a->input_mode) {
  case Tui_input_mode_append:
    app_write_character(a, c);
    break;
  case Tui_input_mode_normal:
  case Tui_input_mode_selresize:
    if (a->tui_cursor.h <= 1 && a->tui_cursor.w <= 1) {
      app_write_character(a, c);
    } else {
      undo_history_push(&a->undo_hist, &a->field, a->tick_num);
      app_fill_selection_with_char(a, c);
      a->needs_remarking = true;
      a->is_draw_dirty = true;
    }
    break;
  case Tui_input_mode_piano:
    app_add_piano_bits_for_character(a, c);
    break;
  }
}

typedef enum {
  App_input_cmd_undo,
  App_input_cmd_toggle_append_mode,
  App_input_cmd_toggle_piano_mode,
  App_input_cmd_toggle_selresize_mode,
  App_input_cmd_step_forward,
  App_input_cmd_toggle_show_event_list,
  App_input_cmd_toggle_play_pause,
  App_input_cmd_cut,
  App_input_cmd_copy,
  App_input_cmd_paste,
  App_input_cmd_escape,
} App_input_cmd;

void app_input_cmd(App_state* a, App_input_cmd ev) {
  switch (ev) {
  case App_input_cmd_undo:
    if (undo_history_count(&a->undo_hist) > 0) {
      if (a->is_playing) {
        undo_history_apply(&a->undo_hist, &a->field, &a->tick_num);
      } else {
        undo_history_pop(&a->undo_hist, &a->field, &a->tick_num);
      }
      tui_cursor_confine(&a->tui_cursor, a->field.height, a->field.width);
      app_make_cursor_visible(a);
      a->needs_remarking = true;
      a->is_draw_dirty = true;
    }
    break;
  case App_input_cmd_toggle_append_mode:
    if (a->input_mode == Tui_input_mode_append) {
      a->input_mode = Tui_input_mode_normal;
    } else {
      a->input_mode = Tui_input_mode_append;
    }
    a->is_draw_dirty = true;
    break;
  case App_input_cmd_toggle_piano_mode:
    if (a->input_mode == Tui_input_mode_piano) {
      a->input_mode = Tui_input_mode_normal;
    } else {
      a->input_mode = Tui_input_mode_piano;
    }
    a->is_draw_dirty = true;
    break;
  case App_input_cmd_toggle_selresize_mode:
    if (a->input_mode == Tui_input_mode_selresize) {
      a->input_mode = Tui_input_mode_normal;
    } else {
      a->input_mode = Tui_input_mode_selresize;
    }
    a->is_draw_dirty = true;
    break;
  case App_input_cmd_step_forward:
    undo_history_push(&a->undo_hist, &a->field, a->tick_num);
    orca_run(a->field.buffer, a->markmap_r.buffer, a->field.height,
             a->field.width, a->tick_num, &a->bank, &a->oevent_list,
             a->piano_bits);
    ++a->tick_num;
    a->piano_bits = ORCA_PIANO_BITS_NONE;
    a->needs_remarking = true;
    a->is_draw_dirty = true;
    break;
  case App_input_cmd_toggle_play_pause:
    if (a->is_playing) {
      app_stop_all_sustained_notes(a);
      a->is_playing = false;
      a->accum_secs = 0.0;
    } else {
      undo_history_push(&a->undo_hist, &a->field, a->tick_num);
      a->is_playing = true;
      // dumb'n'dirty, get us close to the next step time, but not quite
      a->accum_secs = 60.0 / (double)a->bpm / 4.0 - 0.02;
    }
    a->is_draw_dirty = true;
    break;
  case App_input_cmd_toggle_show_event_list:
    a->draw_event_list = !a->draw_event_list;
    a->is_draw_dirty = true;
    break;
  case App_input_cmd_cut: {
    if (app_copy_selection_to_clipbard(a)) {
      undo_history_push(&a->undo_hist, &a->field, a->tick_num);
      app_fill_selection_with_char(a, '.');
      a->needs_remarking = true;
      a->is_draw_dirty = true;
    }
  } break;
  case App_input_cmd_copy: {
    app_copy_selection_to_clipbard(a);
  } break;
  case App_input_cmd_paste: {
    Usz field_h = a->field.height;
    Usz field_w = a->field.width;
    Usz curs_y = a->tui_cursor.y;
    Usz curs_x = a->tui_cursor.x;
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
    a->needs_remarking = true;
    a->is_draw_dirty = true;
  } break;
  case App_input_cmd_escape: {
    if (a->input_mode != Tui_input_mode_normal) {
      a->input_mode = Tui_input_mode_normal;
      a->is_draw_dirty = true;
    } else if (a->tui_cursor.h != 1 || a->tui_cursor.w != 1) {
      a->tui_cursor.h = 1;
      a->tui_cursor.w = 1;
      a->is_draw_dirty = true;
    } else if (a->clipboard_field.height >= 1 &&
               a->clipboard_field.width >= 1) {
      a->tui_cursor.h = a->clipboard_field.height;
      a->tui_cursor.w = a->clipboard_field.width;
      a->is_draw_dirty = true;
    }
  } break;
  }
}

enum {
  Argopt_margins = UCHAR_MAX + 1,
  Argopt_osc_server,
  Argopt_osc_port,
  Argopt_osc_midi_bidule,
};

int main(int argc, char** argv) {
  static struct option tui_options[] = {
      {"margins", required_argument, 0, Argopt_margins},
      {"help", no_argument, 0, 'h'},
      {"osc-server", required_argument, 0, Argopt_osc_server},
      {"osc-port", required_argument, 0, Argopt_osc_port},
      {"osc-midi-bidule", required_argument, 0, Argopt_osc_midi_bidule},
      {NULL, 0, NULL, 0}};
  char* input_file = NULL;
  int margin_thickness = 2;
  char const* osc_hostname = NULL;
  char const* osc_port = NULL;
  Midi_mode midi_mode;
  midi_mode_init(&midi_mode);
  for (;;) {
    int c = getopt_long(argc, argv, "h", tui_options, NULL);
    if (c == -1)
      break;
    switch (c) {
    case 'h':
      usage();
      return 0;
    case Argopt_margins: {
      margin_thickness = atoi(optarg);
      if (margin_thickness == 0 && strcmp(optarg, "0")) {
        fprintf(stderr,
                "Bad margins argument %s.\n"
                "Must be 0 or positive integer.\n",
                optarg);
        return 1;
      }
    } break;
    case Argopt_osc_server: {
      osc_hostname = optarg;
    } break;
    case Argopt_osc_port: {
      osc_port = optarg;
    } break;
    case Argopt_osc_midi_bidule: {
      midi_mode_set_osc_bidule(&midi_mode, optarg);
    } break;
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

  App_state app_state;
  app_init(&app_state);

  if (osc_hostname != NULL && osc_port == NULL) {
    fprintf(stderr,
            "An OSC server address was specified, but no OSC port was "
            "specified.\n"
            "OSC output is not possible without specifying an OSC port.\n");
    app_deinit(&app_state);
    exit(1);
  }
  if (midi_mode.any.type == Midi_mode_type_osc_bidule && osc_port == NULL) {
    fprintf(stderr,
            "MIDI was set to be sent via OSC formatted for Plogue Bidule,\n"
            "but no OSC port was specified.\n"
            "OSC output is not possible without specifying an OSC port.\n");
    app_deinit(&app_state);
    exit(1);
  }
  if (osc_port != NULL) {
    if (!app_set_osc_udp(&app_state, osc_hostname, osc_port)) {
      fprintf(stderr, "Failed to set up OSC networking\n");
      app_deinit(&app_state);
      exit(1);
    }
  }

  if (input_file) {
    Field_load_error fle = field_load_file(input_file, &app_state.field);
    if (fle != Field_load_error_ok) {
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
      app_deinit(&app_state);
      return 1;
    }
  } else {
    input_file = "unnamed";
    field_init_fill(&app_state.field, 25, 57, '.');
  }
  app_state.filename = input_file;
  app_set_midi_mode(&app_state, &midi_mode);

  // Set up timer lib
  stm_setup();

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
  // Don't block on calls like getch() -- have it ERR immediately if the user
  // hasn't typed anything. That way we can mix other timers in our code,
  // instead of being a slave only to terminal input.
  // nodelay(stdscr, TRUE);

  if (has_colors()) {
    // Enable color
    start_color();
    use_default_colors();
    for (int ifg = 0; ifg < Colors_count; ++ifg) {
      for (int ibg = 0; ibg < Colors_count; ++ibg) {
        int res = init_pair((short int)(1 + ifg * Colors_count + ibg),
                            (short int)(ifg - 1), (short int)(ibg - 1));
        (void)res;
        // Might fail on Linux virtual console/terminal for a couple of colors.
        // Just ignore.
#if 0
        if (res == ERR) {
          endwin();
          fprintf(stderr, "Error initializing color pair: %d %d\n", ifg - 1,
                  ibg - 1);
          exit(1);
        }
#endif
      }
    }
  }

  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
  if (has_mouse()) {
    // no waiting for distinguishing click from press
    mouseinterval(0);
  }

  WINDOW* cont_win = NULL;
  int key = KEY_RESIZE;
  wtimeout(stdscr, 0);
  U64 last_time = 0;
  int cur_timeout = 0;

  for (;;) {
    switch (key) {
    case ERR: {
      U64 diff = stm_laptime(&last_time);
      app_apply_delta_secs(&app_state, stm_sec(diff));
      app_do_stuff(&app_state);
      if (app_is_draw_dirty(&app_state)) {
        app_draw(&app_state, cont_win);
      }
      diff = stm_laptime(&last_time);
      app_apply_delta_secs(&app_state, stm_sec(diff));
      double secs_to_d = app_secs_to_deadline(&app_state);
      // fprintf(stderr, "to deadline: %f\n", secs_to_d);
      int new_timeout;
      if (secs_to_d < ms_to_sec(0.5)) {
        new_timeout = 0;
      } else if (secs_to_d < ms_to_sec(3.0)) {
        new_timeout = 1;
      } else if (secs_to_d < ms_to_sec(10.0)) {
        new_timeout = 5;
      } else if (secs_to_d < ms_to_sec(50.0)) {
        new_timeout = 15;
      } else {
        new_timeout = 20;
      }
      if (new_timeout != cur_timeout) {
        wtimeout(stdscr, new_timeout);
        cur_timeout = new_timeout;
      }
    } break;
    case KEY_RESIZE: {
      int term_height = getmaxy(stdscr);
      int term_width = getmaxx(stdscr);
      assert(term_height >= 0 && term_width >= 0);
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
      if (cont_win == NULL || getmaxy(cont_win) != content_h ||
          getmaxx(cont_win) != content_w) {
        if (cont_win) {
          delwin(cont_win);
        }
        wclear(stdscr);
        cont_win = derwin(stdscr, content_h, content_w, content_y, content_x);
        app_set_window_size(&app_state, content_h, content_w);
      }
    } break;
    case KEY_MOUSE: {
      MEVENT mevent;
      if (cont_win && getmouse(&mevent) == OK) {
        int win_y, win_x;
        int win_h, win_w;
        getbegyx(cont_win, win_y, win_x);
        getmaxyx(cont_win, win_h, win_w);
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
        app_mouse_event(&app_state, (Usz)inwin_y, (Usz)inwin_x, mevent.bstate);
      }
    } break;
    case CTRL_PLUS('q'):
      goto quit;
    case KEY_UP:
    case CTRL_PLUS('k'):
      app_dir_input(&app_state, App_dir_up);
      break;
    case CTRL_PLUS('j'):
    case KEY_DOWN:
      app_dir_input(&app_state, App_dir_down);
      break;
    case 127: // backspace in terminal.app, apparently
    case KEY_BACKSPACE:
    case CTRL_PLUS('h'):
    case KEY_LEFT:
      app_dir_input(&app_state, App_dir_left);
      break;
    case CTRL_PLUS('l'):
    case KEY_RIGHT:
      app_dir_input(&app_state, App_dir_right);
      break;
    case CTRL_PLUS('z'):
    case CTRL_PLUS('u'):
      app_input_cmd(&app_state, App_input_cmd_undo);
      break;
    case '[':
      app_adjust_rulers_relative(&app_state, 0, -1);
      break;
    case ']':
      app_adjust_rulers_relative(&app_state, 0, 1);
      break;
    case '{':
      app_adjust_rulers_relative(&app_state, -1, 0);
      break;
    case '}':
      app_adjust_rulers_relative(&app_state, 1, 0);
      break;
    case '(':
      app_resize_grid_relative(&app_state, 0, -1);
      break;
    case ')':
      app_resize_grid_relative(&app_state, 0, 1);
      break;
    case '_':
      app_resize_grid_relative(&app_state, -1, 0);
      break;
    case '+':
      app_resize_grid_relative(&app_state, 1, 0);
      break;
    case '\r':
    case KEY_ENTER:
      app_input_cmd(&app_state, App_input_cmd_toggle_append_mode);
      break;
    case '/':
      app_input_cmd(&app_state, App_input_cmd_toggle_piano_mode);
      break;
    case '<':
      app_adjust_bpm(&app_state, -1);
      break;
    case '>':
      app_adjust_bpm(&app_state, 1);
      break;
    case CTRL_PLUS('f'):
      app_input_cmd(&app_state, App_input_cmd_step_forward);
      break;
    case CTRL_PLUS('e'):
      app_input_cmd(&app_state, App_input_cmd_toggle_show_event_list);
      break;
    case CTRL_PLUS('x'):
      app_input_cmd(&app_state, App_input_cmd_cut);
      break;
    case CTRL_PLUS('c'):
      app_input_cmd(&app_state, App_input_cmd_copy);
      break;
    case CTRL_PLUS('v'):
      app_input_cmd(&app_state, App_input_cmd_paste);
      break;
    case '\'':
      app_input_cmd(&app_state, App_input_cmd_toggle_selresize_mode);
      break;
    case ' ':
      app_input_cmd(&app_state, App_input_cmd_toggle_play_pause);
      // flush lap time -- quick hack to prevent time before hitting spacebar
      // to play being applied as actual playback time
      stm_laptime(&last_time);
      break;
    case 27: // Escape
      app_input_cmd(&app_state, App_input_cmd_escape);
      break;

    // Selection size modification. These may not work in all terminals. (Only
    // tested in xterm so far.)
    case 337: // shift-up
      app_modify_selection_size(&app_state, -1, 0);
      break;
    case 336: // shift-down
      app_modify_selection_size(&app_state, 1, 0);
      break;
    case 393: // shift-left
      app_modify_selection_size(&app_state, 0, -1);
      break;
    case 402: // shift-right
      app_modify_selection_size(&app_state, 0, 1);
      break;

    case 330: // delete?
      app_input_character(&app_state, '.');
      break;

    default:
      if (key >= CHAR_MIN && key <= CHAR_MAX && is_valid_glyph((Glyph)key)) {
        app_input_character(&app_state, (char)key);
      }
#if 0
      else {
        fprintf(stderr, "Unknown key number: %d\n", key);
      }
#endif
      break;
    }
    key = wgetch(stdscr);
    if (cur_timeout != 0) {
      wtimeout(stdscr, 0);
      cur_timeout = 0;
    }
  }
quit:
  app_stop_all_sustained_notes(&app_state);
  if (cont_win) {
    delwin(cont_win);
  }
  endwin();
  app_deinit(&app_state);
  return 0;
}
