#include "base.h"
#include "term_util.h"

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

Qnav_stack qnav_stack;

static struct { int unused; } qmenu_spacer_user_unique;

void qnav_init() {
  qnav_stack.count = 0;
  qnav_stack.stack_changed = false;
  memset(qnav_stack.blocks, 0, sizeof(qnav_stack.blocks));
}
void qnav_deinit() {
  while (qnav_stack.count != 0)
    qnav_stack_pop();
}
void qnav_stack_push(Qblock_type_tag tag, int height, int width, Qblock* out) {
#ifndef NDEBUG
  for (Usz i = 0; i < qnav_stack.count; ++i) {
    assert(qnav_stack.blocks[i] != out);
  }
#endif
  int left;
  if (qnav_stack.count > 0) {
    WINDOW* w = qnav_stack.blocks[qnav_stack.count - 1]->outer_window;
    left = getbegx(w) + getmaxx(w) + 0;
  } else {
    left = 0;
  }
  qnav_stack.blocks[qnav_stack.count] = out;
  ++qnav_stack.count;
  out->title = NULL;
  out->outer_window = newwin(height + 2, width + 3, 0, left);
  out->content_window = derwin(out->outer_window, height, width, 1, 1);
  out->tag = tag;
  qnav_stack.stack_changed = true;
}

Qblock* qnav_top_block() {
  if (qnav_stack.count == 0)
    return NULL;
  return qnav_stack.blocks[qnav_stack.count - 1];
}
void qnav_free_block(Qblock* qb);
void qnav_stack_pop() {
  assert(qnav_stack.count > 0);
  if (qnav_stack.count == 0)
    return;
  Qblock* qb = qnav_stack.blocks[qnav_stack.count - 1];
  WINDOW* content_window = qb->content_window;
  WINDOW* outer_window = qb->outer_window;
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
  wmove(qb->outer_window, 0, 2);
  attr_t attrs = A_NORMAL;
  short pair = 0;
  wattr_get(qb->outer_window, &attrs, &pair, NULL);
  wattrset(qb->outer_window, attr);
  wprintw(qb->outer_window, title);
  wattr_set(qb->outer_window, attrs, pair, NULL);
}

void qblock_set_title(Qblock* qb, char const* title) { qb->title = title; }

void qblock_print_frame(Qblock* qb, bool active) {
  qblock_print_border(qb, active ? A_NORMAL : A_DIM);
  if (qb->title) {
    qblock_print_title(qb, qb->title, active ? A_NORMAL : A_DIM);
  }
}

WINDOW* qmsg_window(Qmsg* qm) { return qm->qblock.content_window; }

void qmsg_set_title(Qmsg* qm, char const* title) {
  qblock_set_title(&qm->qblock, title);
}

Qmsg* qmsg_push(int height, int width) {
  Qmsg* qm = malloc(sizeof(Qmsg));
  qnav_stack_push(Qblock_type_qmsg, height, width, &qm->qblock);
  return qm;
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
  qm->ncurses_menu = NULL;
  qm->ncurses_items[0] = NULL;
  qm->items_count = 0;
  qm->id = id;
  return qm;
}
void qmenu_add_choice(Qmenu* qm, char const* text, int id) {
  ITEM* item = new_item(text, NULL);
  set_item_userptr(item, (void*)(intptr_t)(id));
  qm->ncurses_items[qm->items_count] = item;
  ++qm->items_count;
  qm->ncurses_items[qm->items_count] = NULL;
}
void qmenu_add_spacer(Qmenu* qm) {
  ITEM* item = new_item(" ", NULL);
  item_opts_off(item, O_SELECTABLE);
  set_item_userptr(item, &qmenu_spacer_user_unique);
  qm->ncurses_items[qm->items_count] = item;
  ++qm->items_count;
  qm->ncurses_items[qm->items_count] = NULL;
}
void qmenu_push_to_nav(Qmenu* qm) {
  qm->ncurses_menu = new_menu(qm->ncurses_items);
  set_menu_mark(qm->ncurses_menu, " > ");
  set_menu_fore(qm->ncurses_menu, A_BOLD);
  set_menu_grey(qm->ncurses_menu, A_DIM);
  int menu_min_h, menu_min_w;
  scale_menu(qm->ncurses_menu, &menu_min_h, &menu_min_w);
  qnav_stack_push(Qblock_type_qmenu, menu_min_h, menu_min_w, &qm->qblock);
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

void qnav_free_block(Qblock* qb) {
  switch (qb->tag) {
  case Qblock_type_qmsg: {
    Qmsg* qm = qmsg_of(qb);
    free(qm);
  } break;
  case Qblock_type_qmenu: {
    Qmenu* qm = qmenu_of(qb);
    qmenu_free(qm);
  } break;
  }
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
      if (item_userptr(cur) != &qmenu_spacer_user_unique)
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
      if (item_userptr(cur) != &qmenu_spacer_user_unique)
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
