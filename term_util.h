#pragma once
#include <menu.h>
#include <ncurses.h>

#define CTRL_PLUS(c) ((c)&037)

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

void term_util_init_colors();

typedef enum {
  Qnav_type_qmsg,
  Qnav_type_qmenu,
} Qnav_type_tag;

typedef struct {
  Qnav_type_tag tag;
  WINDOW* outer_window;
  WINDOW* content_window;
} Qnav_block;

typedef struct {
  Qnav_block* blocks[16];
  Usz count;
  bool stack_changed;
} Qnav_stack;

typedef struct {
  Qnav_block nav_block;
} Qmsg;

typedef struct {
  Qnav_block nav_block;
  MENU* ncurses_menu;
  ITEM* ncurses_items[32];
  Usz items_count;
} Qmenu;

typedef enum {
  Qmenu_action_type_canceled,
  Qmenu_action_type_picked,
} Qmenu_action_type;

typedef struct {
  Qmenu_action_type type;
} Qmenu_action_any;

typedef struct {
  Qmenu_action_type type;
  int id;
} Qmenu_action_picked;

typedef union {
  Qmenu_action_any any;
  Qmenu_action_picked picked;
} Qmenu_action;

void qnav_init();
void qnav_deinit();
void qnav_draw_box(Qnav_block* qb);
void qnav_draw_title(Qnav_block* qb, char const* title);
Qnav_block* qnav_top_block();
void qnav_stack_pop();

Qmsg* qmsg_push(int height, int width);
WINDOW* qmsg_window(Qmsg* qm);
bool qmsg_drive(Qmsg* qm, int key);
Qmsg* qmsg_of(Qnav_block* qb);

void qmenu_start(Qmenu* qm);
void qmenu_add_choice(Qmenu* qm, char const* text, int id);
void qmenu_add_spacer(Qmenu* qm);
void qmenu_push_to_nav(Qmenu* qm);
bool qmenu_drive(Qmenu* qm, int key, Qmenu_action* out_action);
Qmenu* qmenu_of(Qnav_block* qb);

extern Qnav_stack qnav_stack;
