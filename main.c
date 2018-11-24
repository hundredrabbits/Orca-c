#include <assert.h>
#include <limits.h>
#include <locale.h>
#include <ncurses.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct {
  chtype* buffer;
  int size_y;
  int size_x;
  chtype fill_char;
} view_state;

void init_view_state(view_state* vs) {
  vs->buffer = NULL;
  vs->size_y = 0;
  vs->size_x = 0;
  vs->fill_char = '?';
}

void deinit_view_state(view_state* vs) {
  // Note that we don't have to check if the buffer was ever actually set to a
  // non-null pointer: `free` does this for us.
  free(vs->buffer);
}

void update_view_state(view_state* vs, int term_height, int term_width,
                       chtype fill_char) {
  bool same_dimensions = vs->size_y == term_height && vs->size_x == term_width;
  bool same_fill_char = vs->fill_char == fill_char;
  // If nothing has changed, we don't have any work to do.
  if (same_dimensions && same_fill_char)
    return;
  if (!same_dimensions) {
    // Note that this doesn't check for overflow. In theory that's unsafe, but
    // really unlikely to happen here.
    size_t term_cells = term_height * term_width;
    size_t new_mem_size = term_cells * sizeof(chtype);

    // 'realloc' is like malloc, but it lets you re-use a buffer instead of
    // having to throw away an old one and create a new one. Oftentimes, the
    // cost of 'realloc' is cheaper than 'malloc' for the C runtime, and it
    // reduces memory fragmentation.
    //
    // It's called 'realloc', but you can also use it even you're starting out
    // with a NULL pointer for your buffer.
    vs->buffer = realloc(vs->buffer, new_mem_size);
    vs->size_y = term_height;
    vs->size_x = term_width;
  }
  if (!same_fill_char) {
    vs->fill_char = fill_char;
  }

  // (Re-)fill the buffer with the new data.
  chtype* buff = vs->buffer;
  // For each row, in the buffer, fill it with an alternating pattern of spaces
  // and the character the user typed.
  for (int iy = 0; iy < term_height; ++iy) {
    // Make a pointer to the start of this line in the buffer. We don't
    // actually have to do this -- we could replace line[ix] with (buff + iy *
    // term_width + ix), but this makes it easier to see what's going on.
    chtype* line = buff + iy * term_width;
    for (int ix = 0; ix < term_width; ++ix) {
      // Note that 'if' here is being used with a numerical value instead a
      // boolean. C doesn't actually have real booleans: a 0 value (whatever
      // the number type happens to be, int, char, etc.) is considered 'false',
      // and anything else is 'true'.
      if ((iy + ix) % 2) {
        line[ix] = ' ';
      } else {
        line[ix] = fill_char;
      }
    }
  }
}

void draw_view_state(view_state* vs) {
  // Loop over each row in the buffer, and send the entire row to ncurses all
  // at once. This is the fastest way to draw to the terminal with ncurses.
  for (int i = 0; i < vs->size_y; ++i) {
    // Move the cursor directly to the start of the row.
    move(i, 0);
    // Send the entire line at once. If it's too long, it will be truncated
    // instead of wrapping.
    //
    // We use addchnstr instead of addchstr (notice the 'n') because we know
    // exactly how long the line is, and we don't have a null terminator in our
    // string. If we tried to use addchstr, it would keep trying to read until
    // it got to the end of our buffer, and then past the end of our buffer
    // into unknown memory, because we don't have a null terminator in it.
    addchnstr(vs->buffer + i * vs->size_x, vs->size_x);
  }
}

int main() {
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
  raw();
  // Don't echo keyboard input
  noecho();
  // Also receive arrow keys, etc.
  keypad(stdscr, TRUE);
  // Hide the terminal cursor
  curs_set(0);

  view_state vs;
  init_view_state(&vs);

  printw("Type any character to fill it in an alternating grid, or\ntype '");
  attron(A_BOLD);
  printw("q");
  attroff(A_BOLD);
  printw("' to quit\n");
  refresh();

  for (;;) {
    chtype ch = getch();
    if (ch == 'q')
      break;
    // ncurses gives us the special value KEY_RESIZE if the user didn't
    // actually type anything, but the terminal resized. If that happens to us,
    // just re-use the fill character from last time.
    if (ch == KEY_RESIZE)
      ch = vs.fill_char;
    int term_height = getmaxy(stdscr);
    int term_width = getmaxx(stdscr);
    assert(term_height >= 0 && term_width >= 0);
    update_view_state(&vs, term_height, term_width, ch);
    draw_view_state(&vs);
    refresh();
  }
  deinit_view_state(&vs);
  endwin();
  return 0;
}
