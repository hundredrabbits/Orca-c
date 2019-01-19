#pragma once
#include "base.h"

typedef enum {
  Oevent_type_midi,
  Oevent_type_osc_ints,
  Oevent_type_udp_string,
} Oevent_types;

typedef struct {
  U8 oevent_type;
} Oevent_any;

typedef struct {
  U8 oevent_type;
  U8 channel;
  U8 octave;
  U8 note;
  U8 velocity;
  U8 bar_divisor;
} Oevent_midi;

enum { Oevent_osc_int_count = 16 };

typedef struct {
  U8 oevent_type;
  Glyph glyph;
  U8 count;
  U8 numbers[Oevent_osc_int_count];
} Oevent_osc_ints;

enum { Oevent_udp_string_count = 16 };

typedef struct {
  U8 oevent_type;
  U8 count;
  char chars[Oevent_udp_string_count];
} Oevent_udp_string;

typedef union {
  Oevent_any any;
  Oevent_midi midi;
  Oevent_osc_ints osc_ints;
  Oevent_udp_string udp_string;
} Oevent;

typedef struct {
  Oevent* buffer;
  Usz count;
  Usz capacity;
} Oevent_list;

void oevent_list_init(Oevent_list* olist);
void oevent_list_deinit(Oevent_list* olist);
void oevent_list_clear(Oevent_list* olist);
ORCA_FORCE_NO_INLINE
void oevent_list_copy(Oevent_list const* src, Oevent_list* dest);
ORCA_FORCE_NO_INLINE
Oevent* oevent_list_alloc_item(Oevent_list* olist);
