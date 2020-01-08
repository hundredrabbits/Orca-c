// Derived from gingerBill's public domain gb_string.h file.
#include "sdd.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if (defined(__GNUC__) || defined(__clang__)) && defined(__has_attribute)
#if __has_attribute(noinline) && __has_attribute(noclone)
#define SDD_NOINLINE __attribute__((noinline, noclone))
#elif __has_attribute(noinline)
#define SDD_NOINLINE __attribute__((noinline))
#endif
#elif defined(_MSC_VER)
#define SDD_NOINLINE __declspec(noinline)
#endif
#ifndef SDD_NOINLINE
#define SDD_NOINLINE
#endif

#define SDD_INTERNAL SDD_NOINLINE static
#define SDD_HDR(s) ((sdd_header *)s - 1)
#define SDD_CAP_MAX (SIZE_MAX - (sizeof(sdd_header) + 1))

typedef struct sdd {
  size_t len;
  size_t cap;
} sdd_header;

SDD_INTERNAL sdd *sdd_impl_new(char const *init, size_t len, size_t cap) {
  if (cap > SDD_CAP_MAX)
    return NULL;
  sdd_header *header = (sdd *)malloc(sizeof(sdd) + cap + 1);
  if (!header)
    return NULL;
  header->len = len;
  header->cap = cap;
  char *str = (char *)(header + 1);
  if (init)
    memcpy(str, init, len);
  str[len] = '\0';
  return (sdd *)str;
}
SDD_INTERNAL sdd *sdd_impl_reallochdr(sdd_header *hdr, size_t new_cap) {
  sdd_header *new_hdr = realloc(hdr, sizeof(sdd_header) + new_cap + 1);
  if (!new_hdr) {
    free(hdr);
    return NULL;
  }
  new_hdr->cap = new_cap;
  return new_hdr + 1;
}
SDD_INTERNAL sdd *sdd_impl_catvprintf(sdd *s, char const *fmt, va_list ap) {
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
  if (!s)
    return NULL;
  vsnprintf((char *)s + old_len, (size_t)required + 1, fmt, ap);
  SDD_HDR(s)->len = old_len + (size_t)required;
  return s;
}

sdd *sdd_new(char const *str) {
  size_t len = strlen(str);
  return sdd_impl_new(str, len, len);
}
sdd *sdd_newlen(char const *init, size_t len) {
  return sdd_impl_new(init, len, len);
}
sdd *sdd_newcap(size_t cap) { return sdd_impl_new(NULL, 0, cap); }
sdd *sdd_dup(sdd const *str) {
  size_t len = SDD_HDR(str)->len;
  return sdd_impl_new((char const *)str, len, len);
}
sdd *sdd_newvprintf(char const *fmt, va_list ap) {
  return sdd_impl_catvprintf(NULL, fmt, ap);
}
sdd *sdd_newprintf(char const *fmt, ...) {
  sdd *s;
  va_list ap;
  va_start(ap, fmt);
  s = sdd_impl_catvprintf(NULL, fmt, ap);
  va_end(ap);
  return s;
}
void sdd_free(sdd *s) {
  if (!s)
    return;
  free(s - 1);
}
sdd *sdd_cpy(sdd *restrict s, char const *restrict cstr) {
  return sdd_cpylen(s, cstr, strlen(cstr));
}
SDD_NOINLINE
sdd *sdd_cpylen(sdd *restrict s, char const *restrict cstr, size_t len) {
  s = sdd_ensurecap(s, len);
  if (!s)
    return NULL;
  SDD_HDR(s)->len = len;
  memcpy(s, cstr, len);
  ((char *)s)[len] = '\0';
  return s;
}
sdd *sdd_cpysdd(sdd *restrict s, sdd const *restrict other) {
  return sdd_cpylen(s, (char const *)other, SDD_HDR(other)->len);
}
sdd *sdd_cat(sdd *restrict s, char const *restrict other) {
  return sdd_catlen(s, other, strlen(other));
}
SDD_NOINLINE
sdd *sdd_catlen(sdd *restrict s, char const *restrict other, size_t other_len) {
  size_t curr_len = SDD_HDR(s)->len;
  s = sdd_makeroomfor(s, other_len);
  if (!s)
    return NULL;
  memcpy((char *)s + curr_len, other, other_len);
  ((char *)s)[curr_len + other_len] = '\0';
  SDD_HDR(s)->len = curr_len + other_len;
  return s;
}
sdd *sdd_catsdd(sdd *restrict s, sdd const *restrict other) {
  return sdd_catlen(s, (char const *)other, SDD_HDR(other)->len);
}
sdd *sdd_catvprintf(sdd *restrict s, char const *fmt, va_list ap) {
  return sdd_impl_catvprintf(s, fmt, ap);
}
sdd *sdd_catprintf(sdd *restrict s, char const *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  s = sdd_impl_catvprintf(s, fmt, ap);
  va_end(ap);
  return s;
}
SDD_NOINLINE
sdd *sdd_ensurecap(sdd *s, size_t new_cap) {
  sdd_header *hdr = SDD_HDR(s);
  if (new_cap > SDD_CAP_MAX) {
    free(hdr);
    return NULL;
  }
  if (hdr->cap >= new_cap)
    return s;
  return sdd_impl_reallochdr(hdr, new_cap);
}
SDD_NOINLINE
sdd *sdd_makeroomfor(sdd *s, size_t add_len) {
  sdd_header *hdr = SDD_HDR(s);
  size_t len = hdr->len, cap = hdr->cap;
  if (len > SDD_CAP_MAX - add_len) { // overflow, goodnight
    free(hdr);
    return NULL;
  }
  size_t new_cap = len + add_len;
  if (cap >= new_cap)
    return s;
  return sdd_impl_reallochdr(hdr, new_cap);
}
size_t sdd_len(sdd const *s) { return SDD_HDR(s)->len; }
size_t sdd_cap(sdd const *s) { return SDD_HDR(s)->cap; }
size_t sdd_avail(sdd const *s) {
  sdd_header *h = SDD_HDR(s);
  return h->cap - h->len;
}
void sdd_clear(sdd *s) {
  SDD_HDR(s)->len = 0;
  ((char *)s)[0] = '\0';
}
void sdd_pokelen(sdd *s, size_t len) { SDD_HDR(s)->len = len; }
void sdd_trim(sdd *restrict s, char const *restrict cut_set) {
  char *str, *start, *end, *start_pos, *end_pos;
  start_pos = start = str = (char *)s;
  end_pos = end = str + SDD_HDR(s)->len - 1;
  while (start_pos <= end && strchr(cut_set, *start_pos))
    start_pos++;
  while (end_pos > start_pos && strchr(cut_set, *end_pos))
    end_pos--;
  size_t len = (start_pos > end_pos) ? 0 : ((size_t)(end_pos - start_pos) + 1);
  SDD_HDR(s)->len = len;
  if (str != start_pos)
    memmove(str, start_pos, len);
  str[len] = '\0';
}

#undef SDD_HDR
#undef SDD_NOINLINE
#undef SDD_CAP_MAX
#undef SDD_INTERNAL
