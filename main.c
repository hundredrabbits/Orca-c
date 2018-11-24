#include <locale.h>
#include <ncurses.h>

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

  printw("Type any character to see it in bold\n");
  refresh();
  int ch = getch();
  printw("Your character:\n");
  attron(A_BOLD);
  printw("  %c\n", ch);
  attroff(A_BOLD);
  printw("Press any key to exit");
  refresh();
  getch();
  endwin();
  return 0;
}
