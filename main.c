#include <ncurses.h>

int main() {
  initscr();            // Initialize ncurses
  raw();                // Receive keyboard input immediately
  noecho();             // Don't echo keyboard input
  keypad(stdscr, TRUE); // Also receive arrow keys, etc.
  curs_set(0);          // Hide the terminal cursor

  printw("Type any character to see it in bold\n");
  refresh();
  int ch = getch();
  printw("Your character:\n");
  attron(A_BOLD);
  printw("  %c\n", ch);
  attroff(A_BOLD);
  printw("Press any key to exit");
  attroff(A_BOLD);
  refresh();
  getch();
  endwin();
  return 0;
}
