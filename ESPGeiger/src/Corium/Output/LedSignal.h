/*
  LedSignal.h - Application LED patterns over EGLed.

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
#ifndef LEDSIGNAL_H
#define LEDSIGNAL_H

#include <stdint.h>
#include <atomic>

namespace LedSignal {

  // poll() owns the pins, the rest queue for it
  extern std::atomic<uint8_t> s_head;
  extern std::atomic<uint8_t> s_tail;
  extern bool s_busy;
  inline bool due() {
    return s_head.load(std::memory_order_acquire) != s_tail.load(std::memory_order_acquire) || s_busy;
  }

  // a row of the app's table, as blinkN
  struct Pattern {
    uint16_t on_ms;
    uint16_t off_ms;
    uint8_t  count;
  };

  // rows in PROGMEM, flash_pin is the engine's, flash_led without it
  void begin(const Pattern* rows, uint8_t count, int flash_pin, bool flash_led);
  void use_pin(int pin, bool low_active);   // led.pin, -1 = none
  void attach();           // last in setup()
  void poll(uint32_t now_us);
  void off();
  void lit(bool on);   // steady

  // caller checks enable and quiet hours
  void flash();

  void play(uint8_t row);   // past the end = nothing

  void setBrightness(uint8_t level);

  // PulseEngine::Mode, caller maps the UI value
  void setFlashConfig(uint8_t engine_mode, uint16_t pulse_us,
                           uint8_t fade_shift_idx, uint8_t max_hz);
}

#endif
