/*
  CounterMeasures.h - Counter history for the outputs.

  Copyright (C) 2026 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#ifndef COUNTER_MEASURES_H
#define COUNTER_MEASURES_H

#include <stddef.h>
#include <stdint.h>
#include "Corium/Measure/EGMeasure.h"

static constexpr uint8_t EGM_OUT_WEBHOOK = EGM_OUT_APP0;
static constexpr uint8_t EGM_OUT_MQTT    = EGM_OUT_APP1;

// before anything reads a row
void measures_begin();
// oldest first, one a second
size_t egm_history(int* out, size_t cap);
// tenths, made on first call
size_t egm_cpm5_history(uint16_t* out, size_t cap);

#endif
