#include <assert.h>
#include <limits.h>
#include <locale.h>
#include <ncurses.h>
#include <stdlib.h>

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

  printw("Type any character to fill it in an alternating grid\n");
  refresh();
  // 'chtype' is the type of character that ncurses uses. It will be an
  // ASCII-like value, if that's what the user hit on the keyboard, but
  // 'chtype' is larger than an 8-bit number and could have something else in
  // it (some Unicode character, a control character for the terminal, etc.)
  chtype ch = getch();
  // We get the dimensions that the terminal is currently set to, so we know
  // how big of a buffer to allocate. We'll fill the buffer with some
  // characters after we've allocated it.
  int term_height = getmaxy(stdscr);
  int term_width = getmaxx(stdscr);
  assert(term_height >= 0 && term_width >= 0);
  // We use 'size_t' when we talk about the size of memory. We also sometimes
  // use it when looping over indices in an array, but we won't do that this
  // time, since we already have the terminal width and height as regular ints.
  size_t term_cells = term_height * term_width;

  // 'calloc' uses the C runtime library to give us a chunk of memory that we
  // can use to do whatever we want. The first argument is the number of things
  // we'll put into the memory, and the second argument is the size of the
  // those things. The total amount of memory it gives us back will be (number
  // of guys * size of guys).
  //
  // There is also another function you may have heard of -- malloc -- which
  // does mostly the same thing. The main differences are that 1) malloc does
  // not turn all of the memory into zeroes before giving it to us, and 2)
  // malloc only takes one argument.
  //
  // Because malloc doesn't zero the memory for us, you have to make sure that
  // you always clear (or write to it) yourself before using it. That wouldn't
  // be a problem in our example, though.
  //
  // Because malloc only takes one argument, you have to do the multiplication
  // yourself, and if you want to be safe about it, you have to check to make
  // sure the multiplication won't overflow. calloc does that for us.
  //
  // sizeof is a special thing that returns the size of an expression or type
  // *at compile time*.
  chtype* buff = calloc(term_cells, sizeof(chtype));

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
        line[ix] = ch;
      }
    }
  }

  // Loop over each row in the buffer, and send the entire row to ncurses all
  // at once. This is the fastest way to draw to the terminal with ncurses.
  for (int i = 0; i < term_height; ++i) {
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
    addchnstr(buff + i * term_width, term_width);
  }

  // We don't need our buffer anymore. We call `free` to return it back to the
  // operating system. If we don't do this, and we lose track of our `buff`
  // pointer, the memory has leaked, and it can't be reclaimed by the OS until
  // the program is terminated.
  free(buff);

  // Refresh the terminal to make sure our changes get displayed immediately.
  refresh();
  // Wair for the user's next input before terminating.
  getch();
  endwin();
  return 0;
}
