#pragma once
#include "base.h"
#include <ncurses.h>

#define CTRL_PLUS(c) ((c)&037)

struct sdd;

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

static ORCA_FORCE_INLINE ORCA_OK_IF_UNUSED
attr_t fg_bg(Color_name fg, Color_name bg) {
  return COLOR_PAIR(1 + fg * Colors_count + bg);
}

void term_util_init_colors(void);

typedef enum {
  Qblock_type_qmsg,
  Qblock_type_qmenu,
  Qblock_type_qform,
} Qblock_type_tag;

typedef struct {
  Qblock_type_tag tag;
  WINDOW* outer_window;
  WINDOW* content_window;
  char const* title;
  int y, x;
} Qblock;

typedef struct {
  Qblock* blocks[16];
  Usz count;
  bool stack_changed;
} Qnav_stack;

typedef struct Qmsg Qmsg;

typedef struct Qmenu Qmenu;

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

typedef struct Qform Qform;

typedef enum {
  Qform_action_type_canceled,
  Qform_action_type_submitted,
} Qform_action_type;
typedef struct {
  Qform_action_type type;
} Qform_action_any;
typedef union {
  Qform_action_any any;
} Qform_action;

void qnav_init(void);
void qnav_deinit(void);
Qblock* qnav_top_block(void);
void qnav_stack_pop(void);

void qblock_print_frame(Qblock* qb, bool active);
void qblock_set_title(Qblock* qb, char const* title);

Qmsg* qmsg_push(int height, int width);
WINDOW* qmsg_window(Qmsg* qm);
void qmsg_set_title(Qmsg* qm, char const* title);
#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif
void qmsg_printf_push(char const* title, char const* fmt, ...);
bool qmsg_drive(Qmsg* qm, int key);
Qmsg* qmsg_of(Qblock* qb);

Qmenu* qmenu_create(int id);
// Useful if menu creation needs to be aborted part-way. Otherwise, no need to
// call -- pushing the qmenu to the qnav stack transfers ownership. (Still
// working on this design, not sure yet.)
void qmenu_destroy(Qmenu* qm);
int qmenu_id(Qmenu const* qm);
void qmenu_set_title(Qmenu* qm, char const* title);
void qmenu_add_choice(Qmenu* qm, int id, char const* text);
void qmenu_add_submenu(Qmenu* qm, int id, char const* text);
#ifdef __GNUC__
__attribute__((format(printf, 3, 4)))
#endif
void qmenu_add_printf(Qmenu* qm, int id, char const* fmt, ...);
void qmenu_add_spacer(Qmenu* qm);
void qmenu_set_current_item(Qmenu* qm, int id);
void qmenu_set_displayed_active(Qmenu* qm, bool active);
void qmenu_push_to_nav(Qmenu* qm);
bool qmenu_drive(Qmenu* qm, int key, Qmenu_action* out_action);
Qmenu* qmenu_of(Qblock* qb);
bool qmenu_top_is_menu(int id);

Qform* qform_create(int id);
int qform_id(Qform const* qf);
Qform* qform_of(Qblock* qb);
void qform_add_text_line(Qform* qf, int id, char const* initial);
void qform_push_to_nav(Qform* qf);
void qform_set_title(Qform* qf, char const* title);
bool qform_drive(Qform* qf, int key, Qform_action* out_action);
bool qform_get_text_line(Qform const* qf, int id, struct sdd** out);

extern Qnav_stack qnav_stack;
