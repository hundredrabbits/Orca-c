// Strings, Dynamic, Dumb
#pragma once
#include <stdarg.h>
#include <stddef.h>

#if (defined(__GNUC__) || defined(__clang__)) && defined(__has_attribute)
#if __has_attribute(format)
#define SDD_PRINTF(n1, n2) __attribute__((format(printf, n1, n2)))
#endif
#if __has_attribute(nonnull)
#define SDD_NONNULL(...) __attribute__((nonnull __VA_ARGS__))
#endif
#if __has_attribute(malloc) && __has_attribute(warn_unused_result)
#define SDD_ALLOC __attribute__((malloc, warn_unused_result))
#elif __has_attribute(warn_unused_result)
#define SDD_ALLOC __attribute__((warn_unused_result))
#endif
#if __has_attribute(warn_unused_result)
#define SDD_USED __attribute__((warn_unused_result))
#endif
#endif
#ifndef SDD_PRINTF
#define SDD_PRINTF(n1, n2)
#endif
#ifndef SDD_NONNULL
#define SDD_NONNULL(...)
#endif
#ifndef SDD_ALLOC
#define SDD_ALLOC
#endif
#ifndef SDD_USED
#define SDD_USED
#endif

typedef struct sdd sdd;

#define sddc(s) ((char *)s)
#define sddcc(s) ((char const *)s)

sdd *sdd_new(char const *s) SDD_NONNULL() SDD_ALLOC;
// ^- Create new with copy of '\0'-terminated cstring.
sdd *sdd_newlen(char const *s, size_t len) SDD_NONNULL() SDD_ALLOC;
// ^- Same, but without calling strlen().
//    Resulting new string will be '\0'-terminated.
sdd *sdd_newcap(size_t cap) SDD_ALLOC;
// ^- 'Raw' new with a specific capacity.
//    Length will be set to 0, and '\0' written at position 0.
sdd *sdd_dup(sdd const *s) SDD_ALLOC;
// ^- Same as sdd_newlen(str, sdd_len(str))
sdd *sdd_newvprintf(char const *fmt, va_list ap) SDD_ALLOC;
sdd *sdd_newprintf(char const *fmt, ...) SDD_PRINTF(1, 2) SDD_ALLOC;
// ^- Create new by using printf
void sdd_free(sdd *s);
// ^- Calling with null is allowed.

sdd *sdd_cpy(sdd *restrict s, char const *restrict cstr) SDD_NONNULL() SDD_USED;
// ^- Set `s` to contain the contents of `cstr`. This is really more like
//    "change into" rather than "copy".
sdd *sdd_cpylen(sdd *restrict s, char const *restrict cstr, size_t len)
    SDD_NONNULL() SDD_USED;
sdd *sdd_cpysdd(sdd *restrict s, sdd const *restrict other);

sdd *sdd_cat(sdd *restrict s, char const *restrict other)
    SDD_NONNULL() SDD_USED;
// ^- Appends contents. The two strings must not overlap in memory.
sdd *sdd_catlen(sdd *restrict s, char const *restrict other, size_t len)
    SDD_NONNULL() SDD_USED;
sdd *sdd_catsdd(sdd *restrict s, sdd const *restrict other)
    SDD_NONNULL() SDD_USED;
sdd *sdd_catvprintf(sdd *restrict s, char const *fmt, va_list ap)
    SDD_NONNULL((1, 2)) SDD_USED;
sdd *sdd_catprintf(sdd *restrict s, char const *fmt, ...) SDD_NONNULL((1, 2))
    SDD_PRINTF(2, 3) SDD_USED;
// ^- Appends by using printf.

size_t sdd_len(sdd const *s) SDD_NONNULL();
// ^- Bytes used by string (excluding '\0' terminator)
size_t sdd_cap(sdd const *s) SDD_NONNULL();
// ^- Bytes allocated on heap (excluding '\0' terminator)
size_t sdd_avail(sdd const *s) SDD_NONNULL();
// ^- sdd_cap(s) - sdd_len(s)

void sdd_clear(sdd *s) SDD_NONNULL();
// ^- Set len to 0, write '\0' at pos 0. Leaves allocated memory in place.
void sdd_pokelen(sdd *s, size_t len) SDD_NONNULL();
// ^- Manually update length field. Doesn't do anything else for you.

void sdd_trim(sdd *restrict s, char const *cut_set) SDD_NONNULL();
// ^- Remove the characters in cut_set from the beginning and ending of s.
sdd *sdd_ensurecap(sdd *s, size_t cap) SDD_NONNULL() SDD_USED;
// ^- Ensure that s has at least cap memory allocated for it. This does not
//    care about the strlen of the characters or the prefixed length count --
//    only the backing memory allocation.
sdd *sdd_makeroomfor(sdd *s, size_t add_len) SDD_NONNULL() SDD_USED;
// ^- Ensure that s has enough allocated space after the valid,
//    null-terminated characters to hold an additional add_len characters. It
//    does not adjust the length, only the capacity, if necessary. Soon after
//    you call sdd_makeroomfor(), you probably will want to call sdd_pokelen(),
//    otherwise you're probably using it incorrectly.

#undef SDD_PRINTF
#undef SDD_NONNULL
#undef SDD_ALLOC
#undef SDD_USED
