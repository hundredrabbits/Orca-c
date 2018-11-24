#include <assert.h>
#include <limits.h>
#include <locale.h>
#include <ncurses.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef char Term;
typedef uint32_t U32;
typedef int32_t I32;

typedef struct {
  Term* buffer;
  U32 height;
  U32 width;
} Field;

void field_init_zeros(Field* f, U32 height, U32 width) {
  size_t num_cells = height * width;
  f->buffer = calloc(num_cells, sizeof(Term));
  f->height = height;
  f->width = width;
}

void field_init_fill(Field* f, U32 height, U32 width, Term fill_char) {
  size_t num_cells = height * width;
  f->buffer = malloc(num_cells * sizeof(Term));
  memset(f->buffer, fill_char, num_cells);
  f->height = height;
  f->width = width;
}

void field_realloc(Field* f, U32 height, U32 width) {
  size_t cells = height * width;
  f->buffer = realloc(f->buffer, cells * sizeof(Term));
  f->height = height;
  f->width = width;
}

void field_deinit(Field* f) {
  assert(f->buffer != NULL);
  free(f->buffer);
#ifndef NDEBUG
  f->buffer = NULL;
#endif
}

void field_copy_subrect(Field* src, Field* dest, U32 src_y, U32 src_x,
                        U32 dest_y, U32 dest_x, U32 height, U32 width) {
  size_t src_height = src->height;
  size_t src_width = src->width;
  size_t dest_height = dest->height;
  size_t dest_width = dest->width;
  if (src_height <= src_y || src_width <= src_x || dest_height <= dest_y ||
      dest_width <= dest_x)
    return;
  size_t ny_0 = src_height - src_y;
  size_t ny_1 = dest_height - dest_y;
  size_t ny = height;
  if (ny_0 < ny)
    ny = ny_0;
  if (ny_1 < ny)
    ny = ny_1;
  if (ny == 0)
    return;
  size_t row_copy_0 = src_width - src_x;
  size_t row_copy_1 = dest_width - dest_x;
  size_t row_copy = width;
  if (row_copy_0 < row_copy)
    row_copy = row_copy_0;
  if (row_copy_1 < row_copy)
    row_copy = row_copy_1;
  size_t copy_bytes = row_copy * sizeof(Term);
  Term* src_p = src->buffer + src_y * src_width + src_x;
  Term* dest_p = dest->buffer + dest_y * dest_width + dest_x;
  size_t src_stride;
  size_t dest_stride;
  if (src_y >= dest_y) {
    src_stride = src_width;
    dest_stride = dest_width;
  } else {
    src_p += (ny - 1) * src_width;
    dest_p += (ny - 1) * dest_width;
    src_stride = -src_width;
    dest_stride = -dest_width;
  }
  size_t iy = 0;
  for (;;) {
    memmove(dest_p, src_p, copy_bytes);
    ++iy;
    if (iy == ny)
      break;
    src_p += src_stride;
    dest_p += dest_stride;
  }
}

void field_fill_subrect(Field* f, U32 y, U32 x, U32 height, U32 width,
                        Term fill_char) {
  size_t f_height = f->height;
  size_t f_width = f->width;
  if (y >= f_height || x >= f_width)
    return;
  size_t rows_0 = f_height - y;
  size_t rows = height;
  if (rows_0 < rows)
    rows = rows_0;
  if (rows == 0)
    return;
  size_t columns_0 = f_width - x;
  size_t columns = width;
  if (columns_0 < columns)
    columns = columns_0;
  size_t fill_bytes = columns * sizeof(Term);
  Term* p = f->buffer + y * f_width + x;
  size_t iy = 0;
  for (;;) {
    memset(p, fill_char, fill_bytes);
    ++iy;
    if (iy == rows)
      break;
    p += f_width;
  }
}

void field_debug_draw(Field* f, int term_y, int term_x) {
  enum { Line_buffer_count = 4096 };
  chtype line_buffer[Line_buffer_count];
  size_t f_height = f->height;
  size_t f_width = f->width;
  Term* f_buffer = f->buffer;
  if (f_width > Line_buffer_count)
    return;
  for (size_t iy = 0; iy < f_height; ++iy) {
    Term* row_p = f_buffer + f_width * iy;
    for (size_t ix = 0; ix < f_width; ++ix) {
      line_buffer[ix] = (chtype)row_p[ix];
    }
    move(iy + term_y, term_x);
    addchnstr(line_buffer, (int)f_width);
  }
}

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
