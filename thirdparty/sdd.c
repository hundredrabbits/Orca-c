#include "sdd.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Derived from gingerBill's public domain gb_string.h file.

/* Examples: */
/* C example */
#if 0
#include "sdd.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
	sdd str = sdd_new("Hello");
	sdd other_str = sdd_newlen(", ", 2);
	str = sdd_catsdd(str, other_str);
	str = sdd_cat(str, "world!");

	printf("%s\n", str); // Hello, world!

	printf("str length = %d\n", sdd_len(str));

	str = sdd_cpy(str, "Potato soup");
	printf("%s\n", str); // Potato soup

	str = sdd_cpy(str, "Hello");
	other_str = sdd_cpy(other_str, "Pizza");
	if (sdd_equal(str, other_str))
		printf("Not called\n");
	else
		printf("Called\n");

	str = sdd_cpy(str, "Ab.;!...AHello World       ??");
	str = sdd_trim(str, "Ab.;!. ?");
	printf("%s\n", str); // "Hello World"

	sdd_free(str);
	sdd_free(other_str);

}
#endif

typedef struct sdd_header {
  size_t len;
  size_t cap;
} sdd_header;

#define SDD_HDR(s) ((sdd_header *)s - 1)

#if defined(__GNUC__) || defined(__clang__)
#define SDD_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define SDD_NOINLINE __declspec(noinline)
#else
#define SDD_NOINLINE
#endif

static void sdd_setcap(sdd str, size_t cap) { SDD_HDR(str)->cap = cap; }

static SDD_NOINLINE sdd sdd_impl_catvprintf(sdd s, char const *fmt,
                                            va_list ap) {
  size_t old_len;
  int required;
  va_list cpy;
  va_copy(cpy, ap);
  required = vsnprintf(NULL, 0, fmt, cpy);
  va_end(cpy);
  if (s) {
    old_len = SDD_HDR(s)->len;
    s = sdd_makeroomfor(s, (size_t)required);
  } else {
    old_len = 0;
    s = sdd_newcap((size_t)required);
  }
  if (s == NULL)
    return NULL;
  vsnprintf(s + old_len, (size_t)required + 1, fmt, ap);
  SDD_HDR(s)->len = old_len + (size_t)required;
  return s;
}

sdd sdd_newcap(size_t cap) {
  sdd_header *header;
  char *str;
  header = (sdd_header *)malloc(sizeof(sdd_header) + cap + 1);
  if (!header)
    return NULL;
  header->len = 0;
  header->cap = cap;
  str = (char *)(header + 1);
  *str = '\0';
  return str;
}

sdd sdd_newlen(void const *init_str, size_t len) {
  sdd_header *header;
  char *str;
  header = (sdd_header *)malloc(sizeof(sdd_header) + len + 1);
  if (!header)
    return NULL;
  header->len = len;
  header->cap = len;
  str = (char *)(header + 1);
  if (len)
    memcpy(str, init_str, len);
  str[len] = '\0';
  return str;
}

sdd sdd_new(char const *str) {
  size_t len = str ? strlen(str) : 0;
  return sdd_newlen(str, len);
}
sdd sdd_newvprintf(char const *fmt, va_list ap) {
  return sdd_impl_catvprintf(NULL, fmt, ap);
}
sdd sdd_newprintf(char const *fmt, ...) {
  sdd s;
  va_list ap;
  va_start(ap, fmt);
  s = sdd_impl_catvprintf(NULL, fmt, ap);
  va_end(ap);
  return s;
}
void sdd_free(sdd str) {
  if (str == NULL)
    return;
  free((sdd_header *)str - 1);
}

sdd sdd_dup(sdd const str) {
  assert(str);
  return sdd_newlen(str, SDD_HDR(str)->len);
}

size_t sdd_len(sdd const str) {
  assert(str);
  return SDD_HDR(str)->len;
}
size_t sdd_cap(sdd const str) {
  assert(str);
  return SDD_HDR(str)->cap;
}

size_t sdd_avail(sdd const str) {
  assert(str);
  sdd_header *h = SDD_HDR(str);
  return h->cap - h->len;
}

void sdd_clear(sdd str) {
  assert(str);
  SDD_HDR(str)->len = 0;
  str[0] = '\0';
}

sdd sdd_catlen(sdd str, char const *other, size_t other_len) {
  assert(str);
  size_t curr_len = SDD_HDR(str)->len;
  str = sdd_makeroomfor(str, other_len);
  if (str == NULL)
    return NULL;
  memcpy(str + curr_len, other, other_len);
  str[curr_len + other_len] = '\0';
  SDD_HDR(str)->len = curr_len + other_len;
  return str;
}

sdd sdd_catsdd(sdd str, sdd const other) {
  return sdd_catlen(str, other, SDD_HDR(other)->len);
}

sdd sdd_cat(sdd str, char const *other) {
  return sdd_catlen(str, other, strlen(other));
}

sdd sdd_cpylen(sdd str, char const *cstr, size_t len) {
  if (sdd_cap(str) < len) {
    str = sdd_makeroomfor(str, len - SDD_HDR(str)->len);
    if (str == NULL)
      return NULL;
  }
  SDD_HDR(str)->len = len;
  memcpy(str, cstr, len);
  str[len] = '\0';
  return str;
}
sdd sdd_cpy(sdd str, char const *cstr) {
  return sdd_cpylen(str, cstr, strlen(cstr));
}

sdd sdd_makeroomfor(sdd str, size_t add_len) {
  size_t len = SDD_HDR(str)->len;
  size_t new_len = len + add_len; // TODO overflow check
  void *ptr, *new_ptr;
  size_t available, new_size;

  available = sdd_avail(str);
  if (available >= add_len) /* Return if there is enough space left */
    return str;
  ptr = (char *)str - sizeof(sdd_header);
  new_size = sizeof(sdd_header) + new_len + 1;
  new_ptr = realloc(ptr, new_size);
  if (new_ptr == NULL) {
    free(ptr);
    return NULL;
  }
  str = (char *)new_ptr + sizeof(sdd_header);
  sdd_setcap(str, new_len);
  return str;
}

void sdd_pokelen(sdd str, size_t len) { SDD_HDR(str)->len = len; }

size_t sdd_totalmemused(sdd const s) {
  size_t cap = sdd_cap(s);
  return sizeof(sdd_header) + cap;
}

bool sdd_equal(sdd const lhs, sdd const rhs) {
  size_t lhs_len = SDD_HDR(lhs)->len;
  size_t rhs_len = SDD_HDR(rhs)->len;
  if (lhs_len != rhs_len)
    return false;
  for (size_t i = 0; i < lhs_len; i++) {
    if (lhs[i] != rhs[i])
      return false;
  }
  return true;
}

sdd sdd_trim(sdd str, char const *cut_set) {
  char *start, *end, *start_pos, *end_pos;
  size_t len;

  start_pos = start = str;
  end_pos = end = str + SDD_HDR(str)->len - 1;

  while (start_pos <= end && strchr(cut_set, *start_pos))
    start_pos++;
  while (end_pos > start_pos && strchr(cut_set, *end_pos))
    end_pos--;

  len = (start_pos > end_pos) ? 0 : ((size_t)(end_pos - start_pos) + 1);

  SDD_HDR(str)->len = len;
  if (str != start_pos)
    memmove(str, start_pos, len);
  str[len] = '\0';
  return str;
}

sdd sdd_catvprintf(sdd s, char const *fmt, va_list ap) {
  // not sure if we should make exception for cat_* functions to allow cat'ing
  // to null pointer. we should see if it ends up being useful in code, or if
  // we should just match the existing behavior of sds/gb_string.
  assert(s != NULL);
  return sdd_impl_catvprintf(s, fmt, ap);
}

sdd sdd_catprintf(sdd s, char const *fmt, ...) {
  assert(s != NULL);
  va_list ap;
  va_start(ap, fmt);
  s = sdd_impl_catvprintf(s, fmt, ap);
  va_end(ap);
  return s;
}

#undef SDD_HDR
#undef SDD_NOINLINE
