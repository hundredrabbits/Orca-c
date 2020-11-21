#include "term_util.h"
#include "oso.h"
#include <ctype.h>
#include <form.h>

void term_util_init_colors() {
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
}

#define ORCA_CONTAINER_OF(ptr, type, member)                                   \
  ((type *)((char *)(1 ? (ptr) : &((type *)0)->member) -                       \
            offsetof(type, member)))

struct Qmsg {
  Qblock qblock;
  Qmsg_dismiss_mode dismiss_mode;
};

typedef struct Qmenu_item {
  char const *text;
  int id;
  U8 owns_string : 1, is_spacer : 1;
} Qmenu_item;

struct Qmenu {
  Qblock qblock;
  Qmenu_item *items;
  Usz items_count, items_cap;
  int current_item, id;
  U8 needs_reprint : 1, is_frontmost : 1;
};

struct Qform {
  Qblock qblock;
  FORM *ncurses_form;
  FIELD *ncurses_fields[32];
  Usz fields_count;
  int id;
};

static void qmenu_free(Qmenu *qm);
static void qform_free(Qform *qf);
ORCA_NOINLINE static void qmenu_reprint(Qmenu *qm);

Qnav_stack qnav_stack;

void qnav_init() { qnav_stack = (Qnav_stack){0}; }
void qnav_deinit() {
  while (qnav_stack.top)
    qnav_stack_pop();
}
// Set new y and x coordinates for the top and left of a Qblock based on the
// position of the Qblock "below" it in the stack. (Below meaning its order in
// the stack, not vertical position on a Y axis.) The target Qblock should
// already be inserted into the stack somewhere, so don't call this before
// you've finished doing the rest of the setup on the Qblock. The y and x
// fields can be junk, though, since this function writes to them without
// reading them.
static ORCA_NOINLINE void qnav_reposition_block(Qblock *qb) {
  int top = 0, left = 0;
  Qblock *prev = qb->down;
  if (!prev)
    goto done;
  int total_h, total_w;
  getmaxyx(qb->outer_window, total_h, total_w);
  WINDOW *w = prev->outer_window;
  int prev_y = prev->y, prev_x = prev->x, prev_h, prev_w;
  getmaxyx(w, prev_h, prev_w);
  // Start by trying to position the item to the right of the previous item.
  left = prev_x + prev_w + 0;
  int term_h, term_w;
  getmaxyx(stdscr, term_h, term_w);
  // Check if we'll run out of room if we position the new item to the right
  // of the existing item (with the same Y position.)
  if (left + total_w > term_w) {
    // If we have enough room if we position just below the previous item in
    // the stack, do that instead of positioning to the right of it.
    if (prev_x + total_w <= term_w && total_h < term_h - (prev_y + prev_h)) {
      top = prev_y + prev_h;
      left = prev_x;
    }
    // If the item doesn't fit there, but it's less wide than the terminal,
    // right-align it to the edge of the terminal.
    else if (total_w < term_w) {
      left = term_w - total_w;
    }
    // Otherwise, just start the layout over at Y=0,X=0
    else {
      left = 0;
    }
  }
done:
  qb->y = top;
  qb->x = left;
}
static ORCA_NOINLINE void qnav_stack_push(Qblock *qb, int height, int width) {
#ifndef NDEBUG
  for (Qblock *i = qnav_stack.top; i; i = i->down) {
    assert(i != qb);
  }
#endif
  int total_h = height + 2, total_w = width + 2;
  if (qnav_stack.top)
    qnav_stack.top->up = qb;
  else
    qnav_stack.bottom = qb;
  qb->down = qnav_stack.top;
  qnav_stack.top = qb;
  qb->outer_window = newpad(total_h, total_w);
  qb->content_window = subpad(qb->outer_window, height, width, 1, 1);
  qnav_reposition_block(qb);
  qnav_stack.occlusion_dirty = true;
}

Qblock *qnav_top_block() { return qnav_stack.top; }

void qblock_init(Qblock *qb, Qblock_type_tag tag) {
  *qb = (Qblock){0};
  qb->tag = tag;
}

void qnav_free_block(Qblock *qb) {
  switch (qb->tag) {
  case Qblock_type_qmsg: {
    Qmsg *qm = qmsg_of(qb);
    free(qm);
    break;
  }
  case Qblock_type_qmenu:
    qmenu_free(qmenu_of(qb));
    break;
  case Qblock_type_qform:
    qform_free(qform_of(qb));
    break;
  }
}

void qnav_stack_pop(void) {
  assert(qnav_stack.top);
  if (!qnav_stack.top)
    return;
  Qblock *qb = qnav_stack.top;
  qnav_stack.top = qb->down;
  if (qnav_stack.top)
    qnav_stack.top->up = NULL;
  else
    qnav_stack.bottom = NULL;
  qnav_stack.occlusion_dirty = true;
  WINDOW *content_window = qb->content_window;
  WINDOW *outer_window = qb->outer_window;
  // erase any stuff underneath where this window is, in case it's outside of
  // the grid in an area that isn't actively redraw
  werase(outer_window);
  wnoutrefresh(outer_window);
  qnav_free_block(qb);
  delwin(content_window);
  delwin(outer_window);
}

bool qnav_draw(void) {
  bool drew_any = false;
  if (!qnav_stack.bottom)
    goto done;
  int term_h, term_w;
  getmaxyx(stdscr, term_h, term_w);
  for (Qblock *qb = qnav_stack.bottom; qb; qb = qb->up) {
    bool is_frontmost = qb == qnav_stack.top;
    if (qnav_stack.occlusion_dirty)
      qblock_print_frame(qb, is_frontmost);
    switch (qb->tag) {
    case Qblock_type_qmsg:
      break;
    case Qblock_type_qmenu: {
      Qmenu *qm = qmenu_of(qb);
      if (qm->is_frontmost != is_frontmost) {
        qm->is_frontmost = is_frontmost;
        qm->needs_reprint = 1;
      }
      if (qm->needs_reprint) {
        qmenu_reprint(qm);
        qm->needs_reprint = 0;
      }
      break;
    }
    case Qblock_type_qform:
      break;
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
    pnoutrefresh(qb->outer_window, 0, 0, qb->y, qb->x, qbwin_endy, qbwin_endx);
    drew_any = true;
  }
done:
  qnav_stack.occlusion_dirty = false;
  return drew_any;
}

void qnav_adjust_term_size(void) {
  if (!qnav_stack.bottom)
    return;
  for (Qblock *qb = qnav_stack.bottom; qb; qb = qb->up)
    qnav_reposition_block(qb);
  qnav_stack.occlusion_dirty = true;
}

void qblock_print_border(Qblock *qb, unsigned int attr) {
  wborder(qb->outer_window, ACS_VLINE | attr, ACS_VLINE | attr,
          ACS_HLINE | attr, ACS_HLINE | attr, ACS_ULCORNER | attr,
          ACS_URCORNER | attr, ACS_LLCORNER | attr, ACS_LRCORNER | attr);
}

void qblock_print_title(Qblock *qb, char const *title, int attr) {
  wmove(qb->outer_window, 0, 1);
  attr_t attrs = A_NORMAL;
  short pair = 0;
  wattr_get(qb->outer_window, &attrs, &pair, NULL);
  wattrset(qb->outer_window, attr);
  waddch(qb->outer_window, ' ');
  waddstr(qb->outer_window, title);
  waddch(qb->outer_window, ' ');
  wattr_set(qb->outer_window, attrs, pair, NULL);
}

void qblock_set_title(Qblock *qb, char const *title) { qb->title = title; }

void qblock_print_frame(Qblock *qb, bool active) {
  qblock_print_border(qb, active ? A_NORMAL : A_DIM);
  if (qb->title) {
    qblock_print_title(qb, qb->title, active ? A_NORMAL : A_DIM);
  }
  if (qb->tag == Qblock_type_qform) {
    Qform *qf = qform_of(qb);
    if (qf->ncurses_form) {
      pos_form_cursor(qf->ncurses_form);
    }
  }
}

WINDOW *qmsg_window(Qmsg *qm) { return qm->qblock.content_window; }

void qmsg_set_title(Qmsg *qm, char const *title) {
  qblock_set_title(&qm->qblock, title);
}

void qmsg_set_dismiss_mode(Qmsg *qm, Qmsg_dismiss_mode mode) {
  if (qm->dismiss_mode == mode)
    return;
  qm->dismiss_mode = mode;
}

Qmsg *qmsg_push(int height, int width) {
  Qmsg *qm = malloc(sizeof(Qmsg));
  qblock_init(&qm->qblock, Qblock_type_qmsg);
  qm->dismiss_mode = Qmsg_dismiss_mode_explicitly;
  qnav_stack_push(&qm->qblock, height, width);
  return qm;
}

Qmsg *qmsg_printf_push(char const *title, char const *fmt, ...) {
  int titlewidth = title ? (int)strlen(title) : 0;
  va_list ap;
  va_start(ap, fmt);
  int msgbytes = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  char *buffer = malloc((Usz)msgbytes + 1);
  if (!buffer)
    exit(1);
  va_start(ap, fmt);
  int printedbytes = vsnprintf(buffer, (Usz)msgbytes + 1, fmt, ap);
  va_end(ap);
  if (printedbytes != msgbytes)
    exit(1); // todo better handling?
  int lines = 1;
  int curlinewidth = 0;
  int maxlinewidth = 0;
  for (int i = 0; i < msgbytes; i++) {
    if (buffer[i] == '\n') {
      buffer[i] = '\0'; // This is terrifying :)
      lines++;
      if (curlinewidth > maxlinewidth)
        maxlinewidth = curlinewidth;
      curlinewidth = 0;
    } else {
      curlinewidth++;
    }
  }
  if (curlinewidth > maxlinewidth)
    maxlinewidth = curlinewidth;
  int width = titlewidth > maxlinewidth ? titlewidth : maxlinewidth;
  width += 2;                          // 1 padding on left and right each
  Qmsg *msg = qmsg_push(lines, width); // no wrapping yet, no real wcwidth, etc
  WINDOW *msgw = qmsg_window(msg);
  int i = 0;
  int offset = 0;
  for (;;) {
    if (offset == msgbytes + 1)
      break;
    int numbytes = (int)strlen(buffer + offset);
    wmove(msgw, i, 1);
    waddstr(msgw, buffer + offset);
    offset += numbytes + 1;
    i++;
  }
  free(buffer);
  if (title)
    qmsg_set_title(msg, title);
  return msg;
}

bool qmsg_drive(Qmsg *qm, int key, Qmsg_action *out_action) {
  *out_action = (Qmsg_action){0};
  Qmsg_dismiss_mode dm = qm->dismiss_mode;
  switch (dm) {
  case Qmsg_dismiss_mode_explicitly:
    break;
  case Qmsg_dismiss_mode_easily:
    out_action->dismiss = true;
    return true;
  case Qmsg_dismiss_mode_passthrough:
    out_action->dismiss = true;
    out_action->passthrough = true;
    return true;
  }
  switch (key) {
  case ' ':
  case 27:
  case '\r':
  case KEY_ENTER:
    out_action->dismiss = true;
    return true;
  }
  return false;
}

Qmsg *qmsg_of(Qblock *qb) { return ORCA_CONTAINER_OF(qb, Qmsg, qblock); }

Qmenu *qmenu_create(int id) {
  Qmenu *qm = (Qmenu *)malloc(sizeof(Qmenu));
  qblock_init(&qm->qblock, Qblock_type_qmenu);
  qm->items = NULL;
  qm->items_count = 0;
  qm->items_cap = 0;
  qm->current_item = 0;
  qm->id = id;
  qm->needs_reprint = 1;
  qm->is_frontmost = 0;
  return qm;
}
void qmenu_destroy(Qmenu *qm) { qmenu_free(qm); }
int qmenu_id(Qmenu const *qm) { return qm->id; }
static ORCA_NOINLINE Qmenu_item *qmenu_allocitems(Qmenu *qm, Usz count) {
  Usz old_count = qm->items_count;
  if (old_count > SIZE_MAX - count) // overflow
    exit(1);
  Usz new_count = old_count + count;
  Usz items_cap = qm->items_cap;
  Qmenu_item *items = qm->items;
  if (new_count > items_cap) {
    // todo overflow check, realloc fail check
    Usz new_cap = new_count < 32 ? 32 : orca_round_up_power2(new_count);
    Usz new_size = new_cap * sizeof(Qmenu_item);
    Qmenu_item *new_items = (Qmenu_item *)realloc(items, new_size);
    if (!new_items)
      exit(1);
    items = new_items;
    items_cap = new_cap;
    qm->items = new_items;
    qm->items_cap = new_cap;
  }
  qm->items_count = new_count;
  return items + old_count;
}
ORCA_NOINLINE static void qmenu_reprint(Qmenu *qm) {
  WINDOW *win = qm->qblock.content_window;
  Qmenu_item *items = qm->items;
  bool isfront = qm->is_frontmost;
  werase(win);
  for (Usz i = 0, n = qm->items_count; i < n; ++i) {
    bool iscur = items[i].id == qm->current_item;
    wattrset(win, isfront ? iscur ? A_BOLD : A_NORMAL : A_DIM);
    wmove(win, (int)i, iscur ? 1 : 3);
    if (iscur)
      waddstr(win, "> ");
    waddstr(win, items[i].text);
  }
}
void qmenu_set_title(Qmenu *qm, char const *title) {
  qblock_set_title(&qm->qblock, title);
}
void qmenu_add_choice(Qmenu *qm, int id, char const *text) {
  assert(id != 0);
  Qmenu_item *item = qmenu_allocitems(qm, 1);
  item->text = text;
  item->id = id;
  item->owns_string = false;
  item->is_spacer = false;
  if (!qm->current_item)
    qm->current_item = id;
}
void qmenu_add_printf(Qmenu *qm, int id, char const *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int textsize = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  char *buffer = malloc((Usz)textsize + 1);
  if (!buffer)
    exit(1);
  va_start(ap, fmt);
  int printedsize = vsnprintf(buffer, (Usz)textsize + 1, fmt, ap);
  va_end(ap);
  if (printedsize != textsize)
    exit(1); // todo better handling?
  Qmenu_item *item = qmenu_allocitems(qm, 1);
  item->text = buffer;
  item->id = id;
  item->owns_string = true;
  item->is_spacer = false;
  if (!qm->current_item)
    qm->current_item = id;
}
void qmenu_add_spacer(Qmenu *qm) {
  Qmenu_item *item = qmenu_allocitems(qm, 1);
  item->text = " ";
  item->id = 0;
  item->owns_string = false;
  item->is_spacer = true;
}
void qmenu_set_current_item(Qmenu *qm, int id) {
  if (qm->current_item == id)
    return;
  qm->current_item = id;
  qm->needs_reprint = 1;
}
int qmenu_current_item(Qmenu *qm) { return qm->current_item; }
void qmenu_push_to_nav(Qmenu *qm) {
  // Probably a programming error if there are no items. Make the menu visible
  // so the programmer knows something went wrong.
  if (qm->items_count == 0)
    qmenu_add_spacer(qm);
  Usz n = qm->items_count;
  Qmenu_item *items = qm->items;
  int menu_min_h = (int)n, menu_min_w = 0;
  for (Usz i = 0; i < n; ++i) {
    int item_w = (int)strlen(items[i].text);
    if (item_w > menu_min_w)
      menu_min_w = item_w;
  }
  menu_min_w += 3 + 1; // left " > " plus 1 empty space on right
  if (qm->qblock.title) {
    // Stupid lack of wcswidth() means we can't know how wide this string is
    // actually displayed. Just fake it for now, until we have Unicode strings
    // in the UI. Then we get sad.
    int title_w = (int)strlen(qm->qblock.title) + 2;
    if (title_w > menu_min_w)
      menu_min_w = title_w;
  }
  qnav_stack_push(&qm->qblock, menu_min_h, menu_min_w);
}

static void qmenu_free(Qmenu *qm) {
  Qmenu_item *items = qm->items;
  for (Usz i = 0, n = qm->items_count; i < n; ++i) {
    if (items[i].owns_string)
      free((void *)items[i].text);
  }
  free(qm->items);
  free(qm);
}

ORCA_NOINLINE static void qmenu_drive_upordown(Qmenu *qm, bool downwards) {
  Qmenu_item *items = qm->items;
  Usz n = qm->items_count;
  if (n <= 1)
    return;
  int cur_id = qm->current_item;
  Usz starting = 0;
  for (; starting < n; ++starting) {
    if (items[starting].id == cur_id)
      goto found;
  }
  return;
found:;
  Usz current = starting;
  for (;;) {
    if (downwards && current < n - 1)
      current++;
    else if (!downwards && current > 0)
      current--;
    if (current == starting)
      break;
    if (!items[current].is_spacer)
      break;
  }
  if (current != starting) {
    qm->current_item = items[current].id;
    qm->needs_reprint = 1;
  }
}

bool qmenu_drive(Qmenu *qm, int key, Qmenu_action *out_action) {
  switch (key) {
  case 27: {
    out_action->any.type = Qmenu_action_type_canceled;
    return true;
  }
  case ' ':
  case '\r':
  case KEY_ENTER:
    out_action->picked.type = Qmenu_action_type_picked;
    out_action->picked.id = qm->current_item;
    return true;
  case KEY_UP:
    qmenu_drive_upordown(qm, false);
    return false;
  case KEY_DOWN:
    qmenu_drive_upordown(qm, true);
    return false;
  }
  return false;
}

Qmenu *qmenu_of(Qblock *qb) { return ORCA_CONTAINER_OF(qb, Qmenu, qblock); }

bool qmenu_top_is_menu(int id) {
  Qblock *qb = qnav_top_block();
  if (!qb)
    return false;
  if (qb->tag != Qblock_type_qmenu)
    return false;
  Qmenu *qm = qmenu_of(qb);
  return qm->id == id;
}

Qform *qform_create(int id) {
  Qform *qf = (Qform *)malloc(sizeof(Qform));
  qblock_init(&qf->qblock, Qblock_type_qform);
  qf->ncurses_form = NULL;
  qf->ncurses_fields[0] = NULL;
  qf->fields_count = 0;
  qf->id = id;
  return qf;
}
static void qform_free(Qform *qf) {
  curs_set(0);
  unpost_form(qf->ncurses_form);
  free_form(qf->ncurses_form);
  for (Usz i = 0; i < qf->fields_count; ++i) {
    free_field(qf->ncurses_fields[i]);
  }
  free(qf);
}
int qform_id(Qform const *qf) { return qf->id; }
Qform *qform_of(Qblock *qb) { return ORCA_CONTAINER_OF(qb, Qform, qblock); }
void qform_set_title(Qform *qf, char const *title) {
  qblock_set_title(&qf->qblock, title);
}
void qform_add_line_input(Qform *qf, int id, char const *initial) {
  FIELD *f = new_field(1, 30, 0, 0, 0, 0);
  if (initial)
    set_field_buffer(f, 0, initial);
  set_field_userptr(f, (void *)(intptr_t)(id));
  field_opts_off(f, O_WRAP | O_BLANK | O_STATIC);
  qf->ncurses_fields[qf->fields_count] = f;
  ++qf->fields_count;
  qf->ncurses_fields[qf->fields_count] = NULL;
}
void qform_push_to_nav(Qform *qf) {
  qf->ncurses_form = new_form(qf->ncurses_fields);
  int form_min_h, form_min_w;
  scale_form(qf->ncurses_form, &form_min_h, &form_min_w);
  qnav_stack_push(&qf->qblock, form_min_h, form_min_w);
  set_form_win(qf->ncurses_form, qf->qblock.outer_window);
  set_form_sub(qf->ncurses_form, qf->qblock.content_window);
  post_form(qf->ncurses_form);
  // quick'n'dirty cursor change for now
  curs_set(1);
  form_driver(qf->ncurses_form, REQ_END_LINE);
}
void qform_single_line_input(int id, char const *title, char const *initial) {
  Qform *qf = qform_create(id);
  qform_set_title(qf, title);
  qform_add_line_input(qf, 1, initial);
  qform_push_to_nav(qf);
}
bool qform_drive(Qform *qf, int key, Qform_action *out_action) {
  switch (key) {
  case 27:
    out_action->any.type = Qform_action_type_canceled;
    return true;
  case CTRL_PLUS('a'):
    form_driver(qf->ncurses_form, REQ_BEG_LINE);
    return false;
  case CTRL_PLUS('e'):
    form_driver(qf->ncurses_form, REQ_END_LINE);
    return false;
  case CTRL_PLUS('b'):
    form_driver(qf->ncurses_form, REQ_PREV_CHAR);
    return false;
  case CTRL_PLUS('f'):
    form_driver(qf->ncurses_form, REQ_NEXT_CHAR);
    return false;
  case CTRL_PLUS('k'):
    form_driver(qf->ncurses_form, REQ_CLR_EOL);
    return false;
  case KEY_RIGHT:
    form_driver(qf->ncurses_form, REQ_RIGHT_CHAR);
    return false;
  case KEY_LEFT:
    form_driver(qf->ncurses_form, REQ_LEFT_CHAR);
    return false;
  case 127: // backspace in terminal.app, apparently
  case KEY_BACKSPACE:
  case CTRL_PLUS('h'):
    form_driver(qf->ncurses_form, REQ_DEL_PREV);
    return false;
  case '\r':
  case KEY_ENTER:
    out_action->any.type = Qform_action_type_submitted;
    return true;
  }
  form_driver(qf->ncurses_form, key);
  return false;
}
static Usz size_without_trailing_spaces(char const *str) {
  Usz size = strlen(str);
  for (;;) {
    if (size == 0)
      break;
    if (!isspace(str[size - 1]))
      break;
    --size;
  }
  return size;
}
static FIELD *qform_find_field(Qform const *qf, int id) {
  Usz count = qf->fields_count;
  for (Usz i = 0; i < count; ++i) {
    FIELD *f = qf->ncurses_fields[i];
    if ((int)(intptr_t)field_userptr(f) == id)
      return f;
  }
  return NULL;
}
bool qform_get_text_line(Qform const *qf, int id, oso **out) {
  FIELD *f = qform_find_field(qf, id);
  if (!f)
    return false;
  form_driver(qf->ncurses_form, REQ_VALIDATION);
  char *buf = field_buffer(f, 0);
  if (!buf)
    return false;
  Usz trimmed = size_without_trailing_spaces(buf);
  osoputlen(out, buf, trimmed);
  return true;
}
bool qform_get_single_text_line(Qform const *qf, struct oso **out) {
  return qform_get_text_line(qf, 1, out);
}
oso *qform_get_nonempty_single_line_input(Qform *qf) {
  oso *s = NULL;
  if (qform_get_text_line(qf, 1, &s) && osolen(s) > 0)
    return s;
  osofree(s);
  return NULL;
}
