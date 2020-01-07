#pragma once
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __GNUC__
#define SDD_PRINTF(n1, n2) __attribute__((format(printf, n1, n2)))
#define SDD_NONNULL(...) __attribute__((nonnull __VA_ARGS__))
#define SDD_ALLOCS __attribute__((malloc, warn_unused_result))
#define SDD_RESULT __attribute__((warn_unused_result))
#else
#define SDD_PRINTF(n1, n2)
#define SDD_NONNULL
#define SDD_ALLOCS
#define SDD_RESULT
#endif

typedef char *sdd;

sdd sdd_new(char const *str) SDD_NONNULL() SDD_ALLOCS;
// ^- Create new with copy of null-terminated cstring.
sdd sdd_newlen(void const *str, size_t len) SDD_NONNULL() SDD_ALLOCS;
// ^- Same, but without calling strlen().
//    Resulting new string will be null terminated.
sdd sdd_newcap(size_t cap) SDD_ALLOCS;
// ^- 'Raw' new with a specific capacity.
//    Length will be set to 0, and '\0' written at position 0.
sdd sdd_dup(sdd const str) SDD_ALLOCS;
// ^- Same as sdd_newlen(str, sdd_len(str))
sdd sdd_newvprintf(char const *fmt, va_list ap) SDD_ALLOCS;
sdd sdd_newprintf(char const *fmt, ...) SDD_PRINTF(1, 2) SDD_ALLOCS;
void sdd_free(sdd str);

sdd sdd_cpy(sdd str, char const *cstr) SDD_RESULT;
// ^- Set `str` to contain the contents of `cstr`
sdd sdd_cpylen(sdd str, char const *cstr, size_t len) SDD_RESULT;

size_t sdd_len(sdd const str) SDD_NONNULL();
// ^- Bytes used by string (excl. null term)
size_t sdd_cap(sdd const str) SDD_NONNULL();
// ^- Bytes allocated on heap (excl. null term)
size_t sdd_avail(sdd const str) SDD_NONNULL();
// ^- cap - len

sdd sdd_cat(sdd str, char const *restrict other) SDD_NONNULL() SDD_RESULT;
sdd sdd_catlen(sdd str, char const *restrict other, size_t len) SDD_RESULT;
sdd sdd_catsdd(sdd str, sdd restrict const other) SDD_RESULT;
sdd sdd_catvprintf(sdd str, char const *fmt, va_list ap) SDD_RESULT;
sdd sdd_catprintf(sdd str, char const *fmt, ...) SDD_PRINTF(2, 3) SDD_RESULT;

void sdd_clear(sdd str) SDD_NONNULL(); // Set len to 0, write '\0' at pos 0
sdd sdd_makeroomfor(sdd str, size_t add_len) SDD_NONNULL() SDD_RESULT;
// ^- Makes sure
void sdd_pokelen(sdd str, size_t len) SDD_NONNULL();
// ^- Manually update length field. Doesn't do anything else for you.

bool sdd_equal(sdd const lhs, sdd const rhs) SDD_NONNULL();

sdd sdd_trim(sdd str, char const *cut_set) SDD_RESULT SDD_NONNULL();

size_t sdd_totalmemused(sdd const str);

#undef SDD_PRINTF
#undef SDD_NONNULL
#undef SDD_ALLOCS
#undef SDD_RESULT
