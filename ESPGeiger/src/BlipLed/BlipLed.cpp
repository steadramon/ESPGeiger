/*
  BlipLed.cpp - see header.

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
#include "App/AppConfig.h"
#ifdef LED_FLASH_ENGINE
#include "BlipLed.h"
#include "Corium/Output/PulseEngine.h"
#include "Corium/System/FastMillis.h"

namespace BlipLed {

  static corium::PulseEngine s_engine;
  volatile uint8_t s_req = 0;
  uint8_t s_seen = 0;
  bool s_busy = false;

  void begin(int pin) {
    s_engine.pin      = pin < 0 ? -1 : (int8_t)pin;
    s_engine.polarity = 0;
    s_engine.commitConfig();
    s_engine.begin();
  }

  void poll(uint32_t now_us) {
    if (s_seen != s_req) {
      s_seen = s_req;
      s_engine.start();
    }
    s_engine.loop(now_us);
    s_busy = s_engine.busy();
  }

  void flash() {
    if (s_engine.take(corium::fastMillis())) s_req++;
  }

  void setBrightness(uint8_t level) {
    s_engine.activeLevel = corium::PulseEngine::levelFrom8bit(level);
  }

  void setConfig(uint8_t engine_mode, uint16_t pulse_us, uint8_t fade_shift_idx, uint8_t max_hz) {
    s_engine.mode       = engine_mode;
    s_engine.pulseUs    = pulse_us;
    s_engine.fadeShift = (uint8_t)(fade_shift_idx + corium::PulseEngine::FADE_SHIFT_MIN);
    s_engine.maxHz      = max_hz;
    s_engine.commitConfig();
  }

}

#endif
