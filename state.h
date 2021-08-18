#pragma once
#include "base.h"
#include "osc_out.h"

typedef struct {
  Usz tick_num;
  Usz bpm;
  bool is_playing : 1;
  Oosc_dev *oosc_dev;

} State;
