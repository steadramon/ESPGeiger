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
#define PCNT_HIGH_LIMIT 32767
#define PCNT_LOW_LIMIT  0

#define PCNT_UNIT PCNT_UNIT_0
#define PCNT_CHANNEL PCNT_CHANNEL_0
// The unit's two bits in ctrl, as in hal/pcnt_ll.h (which is C only)
#define PCNT_RST_BIT   (1u << (2 * PCNT_UNIT))
#define PCNT_PAUSE_BIT (1u << (2 * PCNT_UNIT + 1))

// falling edges, pull set while paused
static inline void pcnt_begin(int pin, gpio_pull_mode_t pull) {
  pcnt_config_t cfg = {
    .pulse_gpio_num = pin,
    .ctrl_gpio_num = -1,
    .pos_mode = PCNT_CHANNEL_EDGE_ACTION_HOLD,
    .neg_mode = PCNT_CHANNEL_EDGE_ACTION_INCREASE,
    .counter_h_lim = PCNT_HIGH_LIMIT,
    .counter_l_lim = PCNT_LOW_LIMIT,
    .unit = PCNT_UNIT,
    .channel = PCNT_CHANNEL,
  };
  pcnt_unit_config(&cfg);
  pcnt_counter_pause(PCNT_UNIT);
  #ifdef PCNT_FILTER
  pcnt_set_filter_value(PCNT_UNIT, PCNT_FILTER);
  pcnt_filter_enable(PCNT_UNIT);
  #endif
  gpio_set_pull_mode((gpio_num_t)pin, pull);
  pcnt_counter_clear(PCNT_UNIT);
  pcnt_counter_resume(PCNT_UNIT);
}

// Paused for a steady read, straight to the registers so the pause is short
static inline int16_t pcnt_take() {
  PCNT.ctrl.val |= PCNT_PAUSE_BIT;
  int16_t n = (int16_t)(uint16_t)PCNT.cnt_unit[PCNT_UNIT].val;
  PCNT.ctrl.val |= PCNT_RST_BIT;
  PCNT.ctrl.val &= ~(PCNT_RST_BIT | PCNT_PAUSE_BIT);
  return n;
}
#endif

#endif
