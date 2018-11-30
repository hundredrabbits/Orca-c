#pragma once
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__GNUC__) || defined(__clang__)
#define ORCA_FORCE_INLINE __attribute__((always_inline)) inline
#define ORCA_FORCE_STATIC_INLINE __attribute__((always_inline)) static inline
#elif defined(_MSC_VER)
#define ORCA_FORCE_INLINE __forceinline
#define ORCA_FORCE_STATIC_INLINE __forceinline static
#else
#define ORCA_FORCE_INLINE inline
#define ORCA_FORCE_STATIC_INLINE inline static
#endif

#if defined(__GNUC__) || defined(__clang__)
#define ORCA_FORCE_NO_INLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define ORCA_FORCE_NO_INLINE __declspec(noinline)
#else
#define ORCA_FORCE_NO_INLINE
#endif

#if defined(__GNUC__) || defined(__clang__)
#define ORCA_ASSUME_ALIGNED(_ptr, _alignment)                                  \
  __builtin_assume_aligned(_ptr, _alignment)
#else
#define ORCA_ASSUME_ALIGNED(_ptr, _alignment) (_ptr)
#endif

#if defined(__GNUC__) || defined(__clang__)
#define ORCA_LIKELY(_x) __builtin_expect(_x, 1)
#else
#define ORCA_LIKELY(_x) (_x)
#endif

#define ORCA_Y_MAX UINT16_MAX
#define ORCA_X_MAX UINT16_MAX

typedef uint8_t U8;
typedef int8_t I8;
typedef uint16_t U16;
typedef int16_t I16;
typedef uint32_t U32;
typedef int32_t I32;
typedef uint64_t U64;
typedef int64_t I64;
typedef size_t Usz;
typedef ssize_t Isz;

typedef char Glyph;

typedef Glyph* Gbuffer;

typedef struct {
  Gbuffer buffer;
  U16 height;
  U16 width;
} Field;

ORCA_FORCE_STATIC_INLINE Usz orca_round_up_power2(Usz x) {
  assert(x <= SIZE_MAX / 2 + 1);
  x -= 1;
  x |= (x >> 1);
  x |= (x >> 2);
  x |= (x >> 4);
  x |= (x >> 8);
  x |= (x >> 16);
#if SIZE_MAX > UINT32_MAX
  x |= (x >> 32);
#endif
  return x + 1;
}
