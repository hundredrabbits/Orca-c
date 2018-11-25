#include "field.h"
#include "sim.h"

static Term const indexed_terms[] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd',
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r',
    's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '.', '*', ':', ';', '#',
};

enum { Terms_array_num = sizeof indexed_terms };

static inline size_t index_of_term(Term c) {
  for (size_t i = 0; i < Terms_array_num; ++i) {
    if (indexed_terms[i] == c)
      return i;
  }
  return SIZE_MAX;
}

static inline Term term_lowered(Term c) {
  return (c >= 'A' && c <= 'Z') ? c - ('a' - 'A') : c;
}

static inline Term terms_sum(Term a, Term b) {
  size_t ia = index_of_term(term_lowered(a));
  size_t ib = index_of_term(term_lowered(b));
  if (ia == SIZE_MAX) ia = 0;
  if (ib == SIZE_MAX) ib = 0;
  return indexed_terms[(ia + ib) % Terms_array_num];
}

static inline Term terms_mod(Term a, Term b) {
  size_t ia = index_of_term(term_lowered(a));
  size_t ib = index_of_term(term_lowered(b));
  if (ia == SIZE_MAX) ia = 0;
  if (ib == SIZE_MAX) ib = 0;
  return indexed_terms[ia % ib];
}

static inline void act_a(Field* f, U32 y, U32 x) {
  Term inp0 = field_peek_relative(f, y, x, 0, 1);
  Term inp1 = field_peek_relative(f, y, x, 0, 2);
  if (inp0 != '.' && inp1 != '.') {
    Term t = terms_sum(inp0, inp1);
    field_poke_relative(f, y, x, 1, 0, t);
  }
}

static inline void act_m(Field* f, U32 y, U32 x) {
  Term inp0 = field_peek_relative(f, y, x, 0, 1);
  Term inp1 = field_peek_relative(f, y, x, 0, 2);
  if (inp0 != '.' && inp1 != '.') {
    Term t = terms_mod(inp0, inp1);
    field_poke_relative(f, y, x, 1, 0, t);
  }
}

void orca_run(Field* f) {
  size_t ny = f->height;
  size_t nx = f->width;
  Term* f_buffer = f->buffer;
  for (size_t iy = 0; iy < ny; ++iy) {
    Term* row = f_buffer + iy * nx;
    for (size_t ix = 0; ix < nx; ++ix) {
      Term c = row[ix];
      switch (c) {
        case 'a':
          act_a(f, iy, ix);
          break;
        case 'm':
          act_m(f, iy, ix);
          break;
      }
    }
  }
}
