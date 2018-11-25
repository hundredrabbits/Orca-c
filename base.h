#pragma once
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ORCA_ROW_MAX UINT16_MAX
#define ORCA_COL_MAX UINT16_MAX

typedef char Term;
typedef uint16_t U16;
typedef int16_t I16;
typedef uint32_t U32;
typedef int32_t I32;
typedef uint64_t U64;
typedef int64_t I64;

typedef struct {
  Term* buffer;
  U32 height;
  U32 width;
} Field;
