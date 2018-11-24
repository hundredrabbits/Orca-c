#pragma once
#include <assert.h>
#include <limits.h>
#include <ncurses.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef char Term;
typedef uint32_t U32;
typedef int32_t I32;

typedef struct {
  Term* buffer;
  U32 height;
  U32 width;
} Field;
