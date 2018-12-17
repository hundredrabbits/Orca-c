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

void term_util_init_colors(void);

typedef enum {
  Qblock_type_qmsg,
  Qblock_type_qmenu,
} Qblock_type_tag;

typedef struct {
  Qblock_type_tag tag;
  WINDOW* outer_window;
  WINDOW* content_window;
  char const* title;
} Qblock;

typedef struct {
  Qblock* blocks[16];
  Usz count;
  bool stack_changed;
} Qnav_stack;

typedef struct {
  Qblock qblock;
} Qmsg;

typedef struct {
  Qblock qblock;
  MENU* ncurses_menu;
  ITEM* ncurses_items[32];
  Usz items_count;
  int id;
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

void qnav_init(void);
void qnav_deinit(void);
Qblock* qnav_top_block(void);
void qnav_stack_pop(void);

void qblock_print_frame(Qblock* qb, bool active);
void qblock_set_title(Qblock* qb, char const* title);

Qmsg* qmsg_push(int height, int width);
WINDOW* qmsg_window(Qmsg* qm);
void qmsg_set_title(Qmsg* qm, char const* title);
bool qmsg_drive(Qmsg* qm, int key);
Qmsg* qmsg_of(Qblock* qb);

Qmenu* qmenu_create(int id);
void qmenu_add_choice(Qmenu* qm, char const* text, int id);
void qmenu_add_spacer(Qmenu* qm);
void qmenu_push_to_nav(Qmenu* qm);
bool qmenu_drive(Qmenu* qm, int key, Qmenu_action* out_action);
Qmenu* qmenu_of(Qblock* qb);
bool qmenu_top_is_menu(int id);

extern Qnav_stack qnav_stack;
