/*
  BlipLed.h - The blip LED on its own pin, a flash per count.

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
#ifndef BLIPLED_H
#define BLIPLED_H

#include <stdint.h>

namespace BlipLed {
  // The ms ticker owns the pin, flash() just posts a request in s_req
  extern volatile uint8_t s_req;
  extern uint8_t s_seen;
  extern bool s_busy;
  inline bool due() { return s_req != s_seen || s_busy; }

  void begin(int pin);   // -1 = none
  void poll(uint32_t now_us);

  // caller checks enable and quiet hours
  void flash();

  void setBrightness(uint8_t level);

  // PulseEngine::Mode, caller maps the UI value
  void setConfig(uint8_t engine_mode, uint16_t pulse_us, uint8_t fade_shift_idx, uint8_t max_hz);
}

#endif
