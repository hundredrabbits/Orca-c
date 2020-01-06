#include "gbstring.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Derived from gingerBill's public domain gb_string.h file.

/* Examples: */
/* C example */
#if 0
#include "gbstring.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
	gbs str = gbs_new("Hello");
	gbs other_str = gbs_newlen(", ", 2);
	str = gbs_catgbs(str, other_str);
	str = gbs_cat(str, "world!");

	printf("%s\n", str); // Hello, world!

	printf("str length = %d\n", gbs_len(str));

	str = gbs_cpy(str, "Potato soup");
	printf("%s\n", str); // Potato soup

	str = gbs_cpy(str, "Hello");
	other_str = gbs_cpy(other_str, "Pizza");
	if (gbs_equal(str, other_str))
		printf("Not called\n");
	else
		printf("Called\n");

	str = gbs_cpy(str, "Ab.;!...AHello World       ??");
	str = gbs_trim(str, "Ab.;!. ?");
	printf("%s\n", str); // "Hello World"

	gbs_free(str);
	gbs_free(other_str);

}
#endif

typedef struct gbStringHeader {
  size_t len;
  size_t cap;
} gbStringHeader;

#define GB_STRING_HEADER(s) ((gbStringHeader *)s - 1)

static void gbs_setlen(gbs str, size_t len) {
  GB_STRING_HEADER(str)->len = len;
}

static void gbs_setcap(gbs str, size_t cap) {
  GB_STRING_HEADER(str)->cap = cap;
}

gbs gbs_newcap(size_t cap) {
  gbStringHeader *header;
  char *str;
  header = (gbStringHeader *)malloc(sizeof(gbStringHeader) + cap + 1);
  if (!header)
    return NULL;
  header->len = 0;
  header->cap = cap;
  str = (char *)(header + 1);
  *str = '\0';
  return str;
}

gbs gbs_newlen(void const *init_str, size_t len) {
  gbStringHeader *header;
  char *str;
  header = (gbStringHeader *)malloc(sizeof(gbStringHeader) + len + 1);
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

gbs gbs_new(char const *str) {
  size_t len = str ? strlen(str) : 0;
  return gbs_newlen(str, len);
}

void gbs_free(gbs str) {
  if (str == NULL)
    return;
  free((gbStringHeader *)str - 1);
}

gbs gbs_dup(gbs const str) { return gbs_newlen(str, gbs_len(str)); }

size_t gbs_len(gbs const str) { return GB_STRING_HEADER(str)->len; }
size_t gbs_cap(gbs const str) { return GB_STRING_HEADER(str)->cap; }

size_t gbs_avail(gbs const str) {
  gbStringHeader *h = GB_STRING_HEADER(str);
  if (h->cap > h->len)
    return h->cap - h->len;
  return 0;
}

void gbs_clear(gbs str) {
  gbs_setlen(str, 0);
  str[0] = '\0';
}

gbs gbs_catlen(gbs str, void const *other, size_t other_len) {
  size_t curr_len = gbs_len(str);
  str = gbs_makeroomfor(str, other_len);
  if (str == NULL)
    return NULL;
  memcpy(str + curr_len, other, other_len);
  str[curr_len + other_len] = '\0';
  gbs_setlen(str, curr_len + other_len);
  return str;
}

gbs gbs_catgbs(gbs str, gbs const other) {
  return gbs_catlen(str, other, gbs_len(other));
}

gbs gbs_cat(gbs str, char const *other) {
  return gbs_catlen(str, other, strlen(other));
}

gbs gbs_cpylen(gbs str, char const *cstr, size_t len) {
  if (gbs_cap(str) < len) {
    str = gbs_makeroomfor(str, len - gbs_len(str));
    if (str == NULL)
      return NULL;
  }
  memcpy(str, cstr, len);
  str[len] = '\0';
  gbs_setlen(str, len);
  return str;
}
gbs gbs_cpy(gbs str, char const *cstr) {
  return gbs_cpylen(str, cstr, strlen(cstr));
}

gbs gbs_makeroomfor(gbs str, size_t add_len) {
  size_t len = gbs_len(str);
  size_t new_len = len + add_len; // TODO overflow check
  void *ptr, *new_ptr;
  size_t available, new_size;

  available = gbs_avail(str);
  if (available >= add_len) /* Return if there is enough space left */
    return str;
  ptr = (char *)str - sizeof(gbStringHeader);
  new_size = sizeof(gbStringHeader) + new_len + 1;
  new_ptr = realloc(ptr, new_size);
  if (new_ptr == NULL) {
    free(ptr);
    return NULL;
  }
  str = (char *)new_ptr + sizeof(gbStringHeader);
  gbs_setcap(str, new_len);
  return str;
}

size_t gbs_totalmemused(gbs const s) {
  size_t cap = gbs_cap(s);
  return sizeof(gbStringHeader) + cap;
}

bool gbs_equal(gbs const lhs, gbs const rhs) {
  size_t lhs_len, rhs_len, i;
  lhs_len = gbs_len(lhs);
  rhs_len = gbs_len(rhs);
  if (lhs_len != rhs_len)
    return false;

  for (i = 0; i < lhs_len; i++) {
    if (lhs[i] != rhs[i])
      return false;
  }

  return true;
}

gbs gbs_trim(gbs str, char const *cut_set) {
  char *start, *end, *start_pos, *end_pos;
  size_t len;

  start_pos = start = str;
  end_pos = end = str + gbs_len(str) - 1;

  while (start_pos <= end && strchr(cut_set, *start_pos))
    start_pos++;
  while (end_pos > start_pos && strchr(cut_set, *end_pos))
    end_pos--;

  len = (start_pos > end_pos) ? 0 : ((size_t)(end_pos - start_pos) + 1);

  if (str != start_pos)
    memmove(str, start_pos, len);
  str[len] = '\0';

  gbs_setlen(str, len);

  return str;
}

gbs gbs_catvprintf(gbs s, const char *fmt, va_list ap) {
  size_t old_len;
  int required;
  va_list cpy;
  va_copy(cpy, ap);
  required = vsnprintf(NULL, 0, fmt, cpy);
  va_end(cpy);
  if (s) {
    old_len = GB_STRING_HEADER(s)->len;
    s = gbs_makeroomfor(s, (size_t)required);
  } else {
    old_len = 0;
    s = gbs_newcap((size_t)required);
  }
  if (s == NULL)
    return NULL;
  va_copy(cpy, ap);
  vsnprintf(s + old_len, (size_t)required + 1, fmt, cpy);
  va_end(cpy);
  return s;
}

gbs gbs_catprintf(gbs s, char const *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  s = gbs_catvprintf(s, fmt, ap);
  va_end(ap);
  return s;
}

#undef GB_STRING_HEADER
