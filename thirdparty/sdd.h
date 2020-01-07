#pragma once
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

typedef char *sdd;

sdd sdd_new(char const *str);
// ^- Create new with copy of null-terminated cstring
sdd sdd_newlen(void const *str, size_t len);
// ^- Same, but without calling strlen().
//    Resulting new string will be null terminated.
sdd sdd_newcap(size_t cap);
// ^- 'Raw' new with a specific capacity.
//    Length will be set to 0, and '\0' written at position 0.
sdd sdd_newvprintf(const char *fmt, va_list ap);
sdd sdd_newprintf(char const *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 1, 2)))
#endif
    ;
void sdd_free(sdd str);

sdd sdd_dup(sdd const str);
// ^- Same as sdd_newlen(str, sdd_len(str))
sdd sdd_cpy(sdd str, char const *cstr);
// ^- Set `str` to contain the contents of `cstr`
sdd sdd_cpylen(sdd str, char const *cstr, size_t len);

size_t sdd_len(sdd const str);   // Bytes used by string (excl. null term)
size_t sdd_cap(sdd const str);   // Bytes allocated on heap (excl. null term)
size_t sdd_avail(sdd const str); // cap - len

sdd sdd_cat(sdd str, char const *other);
sdd sdd_catlen(sdd str, void const *other, size_t len);
sdd sdd_catsdd(sdd str, sdd const other);
sdd sdd_catvprintf(sdd str, const char *fmt, va_list ap);
sdd sdd_catprintf(sdd str, char const *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3)))
#endif
    ;

void sdd_clear(sdd str); // Set len to 0, write '\0' at pos 0
sdd sdd_makeroomfor(sdd str, size_t add_len);
// ^- Makes sure
void sdd_pokelen(sdd str, size_t len);
// ^- Manually update length field. Doesn't do anything else for you.

bool sdd_equal(sdd const lhs, sdd const rhs);

sdd sdd_trim(sdd str, char const *cut_set);

size_t sdd_totalmemused(sdd const str);
