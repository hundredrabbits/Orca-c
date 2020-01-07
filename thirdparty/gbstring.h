#pragma once
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

typedef char *gbs;

gbs gbs_new(char const *str);
// ^- Create new with copy of null-terminated cstring
gbs gbs_newlen(void const *str, size_t len);
// ^- Same, but without calling strlen().
//    Resulting new string will be null terminated.
gbs gbs_newcap(size_t cap);
// ^- 'Raw' new with a specific capacity.
//    Length will be set to 0, and '\0' written at position 0.
gbs gbs_newvprintf(const char *fmt, va_list ap);
gbs gbs_newprintf(char const *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 1, 2)))
#endif
    ;
void gbs_free(gbs str);

gbs gbs_dup(gbs const str);
// ^- Same as gbs_newlen(str, gbs_len(str))
gbs gbs_cpy(gbs str, char const *cstr);
// ^- Set `str` to contain the contents of `cstr`
gbs gbs_cpylen(gbs str, char const *cstr, size_t len);

size_t gbs_len(gbs const str);   // Bytes used by string (excl. null term)
size_t gbs_cap(gbs const str);   // Bytes allocated on heap (excl. null term)
size_t gbs_avail(gbs const str); // cap - len

gbs gbs_cat(gbs str, char const *other);
gbs gbs_catlen(gbs str, void const *other, size_t len);
gbs gbs_catgbs(gbs str, gbs const other);
gbs gbs_catvprintf(gbs str, const char *fmt, va_list ap);
gbs gbs_catprintf(gbs str, char const *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3)))
#endif
    ;

void gbs_clear(gbs str); // Set len to 0, write '\0' at pos 0
gbs gbs_makeroomfor(gbs str, size_t add_len);
// ^- Makes sure
void gbs_pokelen(gbs str, size_t len);
// ^- Manually update length field. Doesn't do anything else for you.

bool gbs_equal(gbs const lhs, gbs const rhs);

gbs gbs_trim(gbs str, char const *cut_set);

size_t gbs_totalmemused(gbs const str);
