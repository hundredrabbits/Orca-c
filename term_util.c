#include "term_util.h"
#include <ctype.h>
#include <form.h>
#include <menu.h>

// No overflow checks in most of these guys. Switch to use 'sds' if we ever
// need anything more advanced.
void heapstr_init(Heapstr* hs) {
  enum { InitialCapacity = 16 };
  hs->str = malloc(InitialCapacity);
  hs->capacity = InitialCapacity;
  hs->str[0] = 0;
}
void heapstr_init_cstr(Heapstr* hs, char const* cstr) {
  Usz len = strlen(cstr);
  hs->str = malloc(len + 1);
  hs->capacity = len + 1;
  memcpy(hs->str, cstr, len + 1);
}
void heapstr_deinit(Heapstr* hs) { free(hs->str); }
void heapstr_reserve(Heapstr* hs, Usz capacity) {
  if (hs->capacity < capacity) {
    Usz new_cap = orca_round_up_power2(capacity);
    hs->str = realloc(hs->str, new_cap);
    hs->capacity = new_cap;
  }
}
void heapstr_set_cstrlen(Heapstr* hs, char const* cstr, Usz len) {
  heapstr_reserve(hs, len + 1);
  memcpy(hs->str, cstr, len);
  hs->str[len] = 0;
}
void heapstr_set_cstr(Heapstr* hs, char const* cstr) {
  Usz len = strlen(cstr);
  heapstr_set_cstrlen(hs, cstr, len);
}
Usz heapstr_len(Heapstr const* hs) { return strlen(hs->str); }

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
  ((type*)((char*)(1 ? (ptr) : &((type*)0)->member) - offsetof(type, member)))

struct Qmsg {
  Qblock qblock;
};

struct Qmenu {
  Qblock qblock;
  MENU* ncurses_menu;
  ITEM* ncurses_items[32];
  Usz items_count;
  ITEM* initial_item;
  int id;
};

struct Qform {
  Qblock qblock;
  FORM* ncurses_form;
  FIELD* ncurses_fields[32];
  Usz fields_count;
  int id;
};

Qnav_stack qnav_stack;

enum {
  Qmenu_spacer_unique_id = INT_MIN + 1,
  Qmenu_first_valid_user_choice_id,
};

void qnav_init() {
  qnav_stack.count = 0;
  qnav_stack.stack_changed = false;
  memset(qnav_stack.blocks, 0, sizeof(qnav_stack.blocks));
}
void qnav_deinit() {
  while (qnav_stack.count != 0)
    qnav_stack_pop();
}
void qnav_stack_push(Qblock* qb, int height, int width) {
#ifndef NDEBUG
  for (Usz i = 0; i < qnav_stack.count; ++i) {
    assert(qnav_stack.blocks[i] != qb);
  }
#endif
  int top = 0, left = 0;
  int totalheight = height + 2, totalwidth = width + 3;
  if (qnav_stack.count > 0) {
    WINDOW* w = qnav_stack.blocks[qnav_stack.count - 1]->outer_window;
    int prev_y, prev_x, prev_h, prev_w;
    getbegyx(w, prev_y, prev_x);
    getmaxyx(w, prev_h, prev_w);
    left = prev_x + prev_w + 0;
    int term_h, term_w;
    getmaxyx(stdscr, term_h, term_w);
    // Check if we'll run out of room if we position the new item to the right
    // of the existing item (with the same Y position.)
    if (left + totalwidth > term_w) {
      // If we have enough room if we position just below the previous item in
      // the stack, do that instead of positioning to the right of it.
      if (prev_x + totalwidth <= term_w &&
          totalheight < term_h - (prev_y + prev_h)) {
        top = prev_y + prev_h;
        left = prev_x;
      }
      // Otherwise, just start the layout over at Y=0,X=0
      else {
        left = 0;
      }
    }
  }
  qnav_stack.blocks[qnav_stack.count] = qb;
  ++qnav_stack.count;
  qb->outer_window = newwin(totalheight, totalwidth, top, left);
  qb->content_window = derwin(qb->outer_window, height, width, 1, 1);
  qnav_stack.stack_changed = true;
}

Qblock* qnav_top_block() {
  if (qnav_stack.count == 0)
    return NULL;
  return qnav_stack.blocks[qnav_stack.count - 1];
}

void qblock_init(Qblock* qb, Qblock_type_tag tag) {
  qb->tag = tag;
  qb->outer_window = NULL;
  qb->content_window = NULL;
  qb->title = NULL;
}

void qmenu_free(Qmenu* qm);
void qform_free(Qform* qf);

void qnav_free_block(Qblock* qb) {
  switch (qb->tag) {
  case Qblock_type_qmsg: {
    Qmsg* qm = qmsg_of(qb);
    free(qm);
  } break;
  case Qblock_type_qmenu: {
    qmenu_free(qmenu_of(qb));
  } break;
  case Qblock_type_qform: {
    qform_free(qform_of(qb));
  } break;
  }
}

void qnav_stack_pop() {
  assert(qnav_stack.count > 0);
  if (qnav_stack.count == 0)
    return;
  Qblock* qb = qnav_stack.blocks[qnav_stack.count - 1];
  WINDOW* content_window = qb->content_window;
  WINDOW* outer_window = qb->outer_window;
  // erase any stuff underneath where this window is, in case it's outside of
  // the grid in an area that isn't actively redraw
  werase(outer_window);
  wnoutrefresh(outer_window);
  qnav_free_block(qb);
  delwin(content_window);
  delwin(outer_window);
  --qnav_stack.count;
  qnav_stack.blocks[qnav_stack.count] = NULL;
  qnav_stack.stack_changed = true;
}

void qblock_print_border(Qblock* qb, unsigned int attr) {
  wborder(qb->outer_window, ACS_VLINE | attr, ACS_VLINE | attr,
          ACS_HLINE | attr, ACS_HLINE | attr, ACS_ULCORNER | attr,
          ACS_URCORNER | attr, ACS_LLCORNER | attr, ACS_LRCORNER | attr);
}

void qblock_print_title(Qblock* qb, char const* title, int attr) {
  wmove(qb->outer_window, 0, 1);
  attr_t attrs = A_NORMAL;
  short pair = 0;
  wattr_get(qb->outer_window, &attrs, &pair, NULL);
  wattrset(qb->outer_window, attr);
  waddch(qb->outer_window, ' ');
  wprintw(qb->outer_window, title);
  waddch(qb->outer_window, ' ');
  wattr_set(qb->outer_window, attrs, pair, NULL);
}

void qblock_set_title(Qblock* qb, char const* title) { qb->title = title; }

void qblock_print_frame(Qblock* qb, bool active) {
  qblock_print_border(qb, active ? A_NORMAL : A_DIM);
  if (qb->title) {
    qblock_print_title(qb, qb->title, active ? A_NORMAL : A_DIM);
  }
  if (qb->tag == Qblock_type_qform) {
    Qform* qf = qform_of(qb);
    if (qf->ncurses_form) {
      pos_form_cursor(qf->ncurses_form);
    }
  }
}

WINDOW* qmsg_window(Qmsg* qm) { return qm->qblock.content_window; }

void qmsg_set_title(Qmsg* qm, char const* title) {
  qblock_set_title(&qm->qblock, title);
}

Qmsg* qmsg_push(int height, int width) {
  Qmsg* qm = malloc(sizeof(Qmsg));
  qblock_init(&qm->qblock, Qblock_type_qmsg);
  qnav_stack_push(&qm->qblock, height, width);
  return qm;
}

void qmsg_printf_push(char const* title, char const* fmt, ...) {
  int titlewidth = title ? (int)strlen(title) : 0;
  va_list ap;
  va_start(ap, fmt);
  int msgbytes = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  char* buffer = malloc((Usz)msgbytes + 1);
  if (!buffer)
    exit(1);
  va_start(ap, fmt);
  vsnprintf(buffer, (Usz)msgbytes + 1, fmt, ap);
  va_end(ap);
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
  int width = titlewidth > maxlinewidth ? titlewidth + 1 : maxlinewidth + 1;
  Qmsg* msg = qmsg_push(lines, width); // no wrapping yet, no real wcwidth, etc
  WINDOW* msgw = qmsg_window(msg);
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
}

bool qmsg_drive(Qmsg* qm, int key) {
  (void)qm;
  switch (key) {
  case ' ':
  case 27:
  case '\r':
  case KEY_ENTER:
    return true;
  }
  return false;
}

Qmsg* qmsg_of(Qblock* qb) { return ORCA_CONTAINER_OF(qb, Qmsg, qblock); }

Qmenu* qmenu_create(int id) {
  Qmenu* qm = (Qmenu*)malloc(sizeof(Qmenu));
  qblock_init(&qm->qblock, Qblock_type_qmenu);
  qm->ncurses_menu = NULL;
  qm->ncurses_items[0] = NULL;
  qm->items_count = 0;
  qm->initial_item = NULL;
  qm->id = id;
  return qm;
}
void qmenu_destroy(Qmenu* qm) { qmenu_free(qm); }
int qmenu_id(Qmenu const* qm) { return qm->id; }
void qmenu_set_title(Qmenu* qm, char const* title) {
  qblock_set_title(&qm->qblock, title);
}
void qmenu_add_choice(Qmenu* qm, char const* text, int id) {
  assert(id >= Qmenu_first_valid_user_choice_id);
  ITEM* item = new_item(text, NULL);
  set_item_userptr(item, (void*)(intptr_t)(id));
  qm->ncurses_items[qm->items_count] = item;
  ++qm->items_count;
  qm->ncurses_items[qm->items_count] = NULL;
}
void qmenu_add_spacer(Qmenu* qm) {
  ITEM* item = new_item(" ", NULL);
  item_opts_off(item, O_SELECTABLE);
  set_item_userptr(item, (void*)(intptr_t)Qmenu_spacer_unique_id);
  qm->ncurses_items[qm->items_count] = item;
  ++qm->items_count;
  qm->ncurses_items[qm->items_count] = NULL;
}
void qmenu_set_current_item(Qmenu* qm, int id) {
  ITEM* found = NULL;
  for (Usz i = 0, n = qm->items_count; i < n; i++) {
    if (item_userptr(qm->ncurses_items[i]) != (void*)(intptr_t)id)
      continue;
    found = qm->ncurses_items[i];
    break;
  }
  if (!found)
    return;
  if (qm->ncurses_menu) {
    set_current_item(qm->ncurses_menu, found);
  } else {
    qm->initial_item = found;
  }
}
void qmenu_set_displayed_active(Qmenu* qm, bool active) {
  // Could add a flag in the Qmenu to avoid redundantly changing this stuff.
  set_menu_fore(qm->ncurses_menu, active ? A_BOLD : A_DIM);
  set_menu_back(qm->ncurses_menu, active ? A_NORMAL : A_DIM);
  set_menu_grey(qm->ncurses_menu, active ? A_DIM : A_DIM);
}
void qmenu_push_to_nav(Qmenu* qm) {
  // new_menu() will get angry if there are no items in the menu. We'll get a
  // null pointer back, and our code will get angry. Instead, just add an empty
  // spacer item. This will probably only ever occur as a programming error,
  // but we should try to avoid having to deal with qmenu_push_to_nav()
  // returning a non-ignorable error for now.
  if (qm->ncurses_items[0] == NULL)
    qmenu_add_spacer(qm);
  qm->ncurses_menu = new_menu(qm->ncurses_items);
  set_menu_mark(qm->ncurses_menu, " > ");
  set_menu_fore(qm->ncurses_menu, A_BOLD);
  set_menu_grey(qm->ncurses_menu, A_DIM);
  int menu_min_h, menu_min_w;
  scale_menu(qm->ncurses_menu, &menu_min_h, &menu_min_w);
  if (qm->qblock.title) {
    // Stupid lack of wcswidth() means we can't know how wide this string is
    // actually displayed. Just fake it for now, until we have Unicode strings
    // in the UI. Then we get sad.
    int title_w = (int)strlen(qm->qblock.title) + 1;
    if (title_w > menu_min_w)
      menu_min_w = title_w;
  }
  if (qm->initial_item)
    set_current_item(qm->ncurses_menu, qm->initial_item);
  qnav_stack_push(&qm->qblock, menu_min_h, menu_min_w);
  set_menu_win(qm->ncurses_menu, qm->qblock.outer_window);
  set_menu_sub(qm->ncurses_menu, qm->qblock.content_window);
  post_menu(qm->ncurses_menu);
}

void qmenu_free(Qmenu* qm) {
  unpost_menu(qm->ncurses_menu);
  free_menu(qm->ncurses_menu);
  for (Usz i = 0; i < qm->items_count; ++i) {
    free_item(qm->ncurses_items[i]);
  }
  free(qm);
}

bool qmenu_drive(Qmenu* qm, int key, Qmenu_action* out_action) {
  switch (key) {
  case 27: {
    out_action->any.type = Qmenu_action_type_canceled;
    return true;
  }
  case ' ':
  case '\r':
  case KEY_ENTER: {
    ITEM* cur = current_item(qm->ncurses_menu);
    out_action->picked.type = Qmenu_action_type_picked;
    out_action->picked.id = cur ? (int)(intptr_t)item_userptr(cur) : 0;
    return true;
  } break;
  case KEY_UP: {
    ITEM* starting = current_item(qm->ncurses_menu);
    menu_driver(qm->ncurses_menu, REQ_UP_ITEM);
    for (;;) {
      ITEM* cur = current_item(qm->ncurses_menu);
      if (!cur || cur == starting)
        break;
      if (item_userptr(cur) != (void*)(intptr_t)Qmenu_spacer_unique_id)
        break;
      menu_driver(qm->ncurses_menu, REQ_UP_ITEM);
    }
    return false;
  }
  case KEY_DOWN: {
    ITEM* starting = current_item(qm->ncurses_menu);
    menu_driver(qm->ncurses_menu, REQ_DOWN_ITEM);
    for (;;) {
      ITEM* cur = current_item(qm->ncurses_menu);
      if (!cur || cur == starting)
        break;
      if (item_userptr(cur) != (void*)(intptr_t)Qmenu_spacer_unique_id)
        break;
      menu_driver(qm->ncurses_menu, REQ_DOWN_ITEM);
    }
    return false;
  }
  }
  return false;
}

Qmenu* qmenu_of(Qblock* qb) { return ORCA_CONTAINER_OF(qb, Qmenu, qblock); }

bool qmenu_top_is_menu(int id) {
  Qblock* qb = qnav_top_block();
  if (!qb)
    return false;
  if (qb->tag != Qblock_type_qmenu)
    return false;
  Qmenu* qm = qmenu_of(qb);
  return qm->id == id;
}

Qform* qform_create(int id) {
  Qform* qf = (Qform*)malloc(sizeof(Qform));
  qblock_init(&qf->qblock, Qblock_type_qform);
  qf->ncurses_form = NULL;
  qf->ncurses_fields[0] = NULL;
  qf->fields_count = 0;
  qf->id = id;
  return qf;
}

Qform* qform_of(Qblock* qb) { return ORCA_CONTAINER_OF(qb, Qform, qblock); }

int qform_id(Qform const* qf) { return qf->id; }

void qform_add_text_line(Qform* qf, int id, char const* initial) {
  FIELD* f = new_field(1, 30, 0, 0, 0, 0);
  set_field_buffer(f, 0, initial);
  set_field_userptr(f, (void*)(intptr_t)(id));
  field_opts_off(f, O_WRAP | O_BLANK | O_STATIC);
  qf->ncurses_fields[qf->fields_count] = f;
  ++qf->fields_count;
  qf->ncurses_fields[qf->fields_count] = NULL;
}

void qform_push_to_nav(Qform* qf) {
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

void qform_set_title(Qform* qf, char const* title) {
  qblock_set_title(&qf->qblock, title);
}

void qform_free(Qform* qf) {
  curs_set(0);
  unpost_form(qf->ncurses_form);
  free_form(qf->ncurses_form);
  for (Usz i = 0; i < qf->fields_count; ++i) {
    free_field(qf->ncurses_fields[i]);
  }
  free(qf);
}

bool qform_drive(Qform* qf, int key, Qform_action* out_action) {
  switch (key) {
  case 27: {
    out_action->any.type = Qform_action_type_canceled;
    return true;
  }
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
  case KEY_ENTER: {
    out_action->any.type = Qform_action_type_submitted;
    return true;
  } break;
  default:
    form_driver(qf->ncurses_form, key);
    return false;
  }
  return false;
}

static Usz size_without_trailing_spaces(char const* str) {
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

FIELD* qform_find_field(Qform const* qf, int id) {
  Usz count = qf->fields_count;
  for (Usz i = 0; i < count; ++i) {
    FIELD* f = qf->ncurses_fields[i];
    if ((int)(intptr_t)field_userptr(f) == id)
      return f;
  }
  return NULL;
}

bool qform_get_text_line(Qform const* qf, int id, Heapstr* out) {
  FIELD* f = qform_find_field(qf, id);
  if (!f)
    return false;
  form_driver(qf->ncurses_form, REQ_VALIDATION);
  char* buf = field_buffer(f, 0);
  if (!buf)
    return false;
  Usz trimmed = size_without_trailing_spaces(buf);
  heapstr_set_cstrlen(out, buf, trimmed);
  return true;
}
