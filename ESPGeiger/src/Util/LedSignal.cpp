/*
  LedSignal.cpp - see header.

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
#include "LedSignal.h"
#include <EGLed.h>
#include "FastMillis.h"
#include "Globals.h"
#include "PulseEngine.h"

namespace LedSignal {

  static EGLed s_onboard(LED_SEND_RECEIVE, LED_SEND_RECEIVE_ON == LOW);

#ifdef GEIGER_BLIPLED
  // PulseEngine owns the external pin alone; its digitalWrite path would
  // otherwise fight the LEDC-driven onboard LED.
  static PulseEngine s_engine;
#endif
  static uint16_t s_click_pulse_ms = 20;
  static uint8_t  s_click_mode     = 0;   // 0=pulse, 2=fade
  static uint8_t  s_click_fade     = 3;

  void begin() {
    EGLed::setClock(fast_millis);
#ifdef GEIGER_BLIPLED
    s_engine.pin      = GEIGER_BLIPLED;
    s_engine.polarity = 0;
    s_engine.commitConfig();
    s_engine.begin();
#endif
  }

  void poll() {
    if (!s_onboard.isRunning()
#ifdef GEIGER_BLIPLED
        && s_engine.phases_remaining == 0
        && s_engine.brightness == 0
#endif
       ) return;
    s_onboard.update();
#ifdef GEIGER_BLIPLED
    s_engine.loop();
#endif
  }

  void off() {
    s_onboard.off();
  }

  void click() {
#if defined(GEIGER_BLIPLED)
    s_engine.notifyClick(fast_millis());
#elif !defined(DISABLE_INTERNAL_BLIP)
    if (s_onboard.isRunning()) return;
#ifndef EGPE_NO_PWM
    if (s_click_mode == 2) { s_onboard.fade(s_click_fade); return; }
#endif
    s_onboard.pulse(s_click_pulse_ms);
#endif
  }

  void activity() {
    s_onboard.blink(500, 500);
  }

  void shortPressAck() {
    s_onboard.pulse(200);
  }

  void displayEnabled()  {}
  void displayDisabled() {}

  void setBrightness(uint8_t level) {
    s_onboard.setBrightness(level);
#ifdef GEIGER_BLIPLED
    s_engine.active_level = level;
#endif
  }

  void setBlipEngineConfig(uint8_t engine_mode, uint16_t pulse_us,
                           uint16_t freq_hz, uint16_t cycles,
                           uint8_t fade_shift_idx, uint8_t max_hz) {
    if (engine_mode > 2) engine_mode = 0;
    if (pulse_us < 100)   pulse_us = 100;
    if (pulse_us > 50000) pulse_us = 50000;
    if (freq_hz < 1000)   freq_hz = 1000;
    if (freq_hz > 8000)   freq_hz = 8000;
    if (cycles < 1)       cycles = 1;
    if (cycles > 500)     cycles = 500;
    if (fade_shift_idx > 2) fade_shift_idx = 1;
    if (max_hz > 200) max_hz = 200;
    uint16_t ms = pulse_us / 1000;
    if (ms < 1)   ms = 1;
    if (ms > 100) ms = 100;
    s_click_pulse_ms = ms;
    s_click_mode     = engine_mode;
    s_click_fade     = (uint8_t)(fade_shift_idx + 2);
#ifdef GEIGER_BLIPLED
    s_engine.mode       = engine_mode;
    s_engine.pulse_us   = pulse_us;
    s_engine.freq_hz    = freq_hz;
    s_engine.cycles     = cycles;
    s_engine.fade_shift = fade_shift_idx + 2;
    s_engine.max_hz     = max_hz;
    s_engine.commitConfig();
#else
    (void)freq_hz; (void)cycles; (void)max_hz;
#endif
  }

}
