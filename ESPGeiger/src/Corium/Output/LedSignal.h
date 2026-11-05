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
#ifndef CORIUM_LEDSIGNAL_H
#define CORIUM_LEDSIGNAL_H

#include <stdint.h>
#include <atomic>
#include "App/AppConfig.h"

// 0 = counts never flash this LED, the app has a pin of its own
#ifndef CORIUM_LEDSIGNAL_FLASH
#define CORIUM_LEDSIGNAL_FLASH 1
#endif

namespace corium {
namespace LedSignal {

  // Only poll() touches the pins, everything else queues for it
  extern std::atomic<uint8_t> s_head;
  extern std::atomic<uint8_t> s_tail;
  extern bool s_busy;
  inline bool due() {
    return s_head.load(std::memory_order_acquire) != s_tail.load(std::memory_order_acquire) || s_busy;
  }

  // A row of the app's table, same args as blinkN
  struct Pattern {
    uint16_t onMs;
    uint16_t offMs;
    uint8_t  count;
  };

  // rows in PROGMEM, flash_led = counts flash this LED
  void begin(const Pattern* rows, uint8_t count, bool flash_led);
  void usePin(int pin, bool low_active);   // led.pin, -1 = none
  void attach();           // Last in setup()
  void poll();
  void off();
  void lit(bool on);   // Steady

#if CORIUM_LEDSIGNAL_FLASH
  // Caller checks enable and quiet hours
  void flash();
  // engine_mode is a PulseEngine::Mode, the caller maps the UI value
  void setFlashConfig(uint8_t engine_mode, uint16_t pulse_us, uint8_t fade_shift_idx);
#endif

  void play(uint8_t row);   // Past the end = nothing

  void setBrightness(uint8_t level);
}
}  // namespace corium

#endif
