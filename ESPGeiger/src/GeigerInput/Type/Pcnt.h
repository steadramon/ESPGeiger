/*
  GeigerInput/Type/Pcnt.h - ESP32 pulse counter unit, shared by the pulse types

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
#ifndef GEIGER_PCNT_H
#define GEIGER_PCNT_H

#include "GeigerInput/GeigerInput.h"  // USE_PCNT

#ifdef USE_PCNT
extern "C" {
  #include "soc/pcnt_struct.h"
}
#include <driver/pcnt.h>
#define PCNT_HIGH_LIMIT 32767  // largest +ve value for int16_t.
#define PCNT_LOW_LIMIT  0

#define PCNT_UNIT PCNT_UNIT_0
#define PCNT_CHANNEL PCNT_CHANNEL_0

// paused so nothing lands between read and clear
static inline int16_t pcnt_take() {
  int16_t n = 0;
  pcnt_counter_pause(PCNT_UNIT);
  pcnt_get_counter_value(PCNT_UNIT, &n);
  pcnt_counter_clear(PCNT_UNIT);
  pcnt_counter_resume(PCNT_UNIT);
  return n;
}
#endif

#endif
