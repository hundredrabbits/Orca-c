#include "field.h"
#include "sim.h"

static Term const indexed_terms[] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd',
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r',
    's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '.', '*', ':', ';', '#',
};

enum { Terms_array_num = sizeof indexed_terms };

static inline USz index_of_term(Term c) {
  for (USz i = 0; i < Terms_array_num; ++i) {
    if (indexed_terms[i] == c)
      return i;
  }
  return SIZE_MAX;
}

static inline Term term_lowered(Term c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c - ('a' - 'A')) : c;
}

// Always returns 0 through (sizeof indexed_terms) - 1, and works on
// capitalized terms as well. The index of the lower-cased term is returned if
// the term is capitalized.
static inline USz semantic_index_of_term(Term c) {
  Term c0 = term_lowered(c);
  for (USz i = 0; i < Terms_array_num; ++i) {
    if (indexed_terms[i] == c0)
      return i;
  }
  return 0;
}

static inline Term terms_sum(Term a, Term b) {
  USz ia = semantic_index_of_term(a);
  USz ib = semantic_index_of_term(b);
  return indexed_terms[(ia + ib) % Terms_array_num];
}

static inline Term terms_mod(Term a, Term b) {
  USz ia = semantic_index_of_term(a);
  USz ib = semantic_index_of_term(b);
  return indexed_terms[ib == 0 ? 0 : (ia % ib)];
}

static inline void act_a(Field* f, USz y, USz x) {
  Term inp0 = field_peek_relative(f, y, x, 0, 1);
  Term inp1 = field_peek_relative(f, y, x, 0, 2);
  if (inp0 != '.' && inp1 != '.') {
    Term t = terms_sum(inp0, inp1);
    field_poke_relative(f, y, x, 1, 0, t);
  }
}

static inline void act_m(Field* f, USz y, USz x) {
  Term inp0 = field_peek_relative(f, y, x, 0, 1);
  Term inp1 = field_peek_relative(f, y, x, 0, 2);
  if (inp0 != '.' && inp1 != '.') {
    Term t = terms_mod(inp0, inp1);
    field_poke_relative(f, y, x, 1, 0, t);
  }
}

void orca_run(Field* f) {
  USz ny = f->height;
  USz nx = f->width;
  Term* f_buffer = f->buffer;
  for (USz iy = 0; iy < ny; ++iy) {
    Term* row = f_buffer + iy * nx;
    for (USz ix = 0; ix < nx; ++ix) {
      Term c = row[ix];
      switch (c) {
      case 'a':
        act_a(f, (U32)iy, (U32)ix);
        break;
      case 'm':
        act_m(f, (U32)iy, (U32)ix);
        break;
      }
    }
  }
}
