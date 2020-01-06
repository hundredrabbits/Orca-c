#pragma once
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

typedef char *gbs;

gbs gbs_newcap(size_t cap);
gbs gbs_newlen(void const *str, size_t len);
gbs gbs_new(char const *str);
void gbs_free(gbs str);

gbs gbs_dup(gbs const str);

size_t gbs_len(gbs const str);
size_t gbs_cap(gbs const str);
size_t gbs_avail(gbs const str);

void gbs_clear(gbs str);

gbs gbs_catlen(gbs str, void const *other, size_t len);
gbs gbs_cat(gbs str, char const *other);
gbs gbs_catgbs(gbs str, gbs const other);

gbs gbs_cpylen(gbs str, char const *cstr, size_t len);
gbs gbs_cpy(gbs str, char const *cstr);

gbs gbs_makeroomfor(gbs str, size_t add_len);

bool gbs_streq(gbs const lhs, gbs const rhs);

gbs gbs_trim(gbs str, char const *cut_set);

gbs gbs_catvprintf(gbs str, const char *fmt, va_list ap);
gbs gbs_catprintf(gbs str, char const *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3)))
#endif
    ;

size_t gbs_totalmemused(gbs const str);
