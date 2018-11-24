#include "base.h"
#include "field.h"
#include <locale.h>
#include <unistd.h>

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
  // Don't block on calls like getch() -- have it ERR immediately if the user
  // hasn't typed anything. That way we can mix other timers in our code,
  // instead of being a slave only to terminal input.
  // nodelay(stdscr, TRUE);

  Field field;
  field_init_fill(&field, 16, 16, '.');

  printw("Type any character to fill it in an alternating grid, or\ntype '");
  attron(A_BOLD);
  printw("q");
  attroff(A_BOLD);
  printw("' to quit\n");
  refresh();

  char fill_char = '?';
  for (;;) {
    int ch = getch();
    clear();
    if (ch == 'q')
      break;
    // ncurses gives us ERR if there was no user input. We'll sleep for 0
    // seconds, so that we'll yield CPU time to the OS instead of looping as
    // fast as possible. This avoids battery drain/excessive CPU usage. There
    // are better ways to do this that waste less CPU, but they require doing a
    // little more work on each individual platform (Linux, Mac, etc.)
    if (ch == ERR) {
      sleep(0);
      continue;
    }
    // ncurses gives us the special value KEY_RESIZE if the user didn't
    // actually type anything, but the terminal resized. If that happens to us,
    // just re-use the fill character from last time.
    char new_fill_char;
    if (ch < CHAR_MIN || ch > CHAR_MAX || ch == KEY_RESIZE)
      new_fill_char = '?';
    else
      new_fill_char = (char)ch;
    int term_height = getmaxy(stdscr);
    int term_width = getmaxx(stdscr);
    assert(term_height >= 0 && term_width >= 0);
    if (new_fill_char != fill_char) {
      fill_char = new_fill_char;
    }
    field_fill_subrect(&field, 1, 1, field.height - 2, field.width - 2,
                       fill_char);
    field_debug_draw(&field, 0, 0);
    field_copy_subrect(&field, &field, 0, 0, 4, 4, 8, 8);
    field_copy_subrect(&field, &field, 0, 0, 0, 0, 0, 0);
    field_debug_draw(&field, field.height + 1, 0);
    field_copy_subrect(&field, &field, 6, 6, 9, 9, 30, 30);
    field_debug_draw(&field, 0, field.width + 1);
    refresh();
  }
  field_deinit(&field);
  endwin();
  return 0;
}
