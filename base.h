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
#elif defined(_MSC_VER)
#define ORCA_FORCE_INLINE __forceinline
#else
#define ORCA_FORCE_INLINE inline
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

typedef struct {
  Glyph* buffer;
  U16 height;
  U16 width;
} Field;
