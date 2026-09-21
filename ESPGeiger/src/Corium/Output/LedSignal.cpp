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
#include "../Logger/Logger.h"
#include "../System/FastMillis.h"
#include "../System/Globals.h"
#include "../System/PinSafety.h"
#include "PulseEngine.h"

namespace LedSignal {

  // dark till attach(), the pin comes from led.pin
  static EGLed s_onboard(EGLed::NO_PIN);
  static int8_t s_pin = -1;   // none in safe mode, prefs never load
  static bool   s_low = false;

#ifdef BLIP_LED_PIN
  // PulseEngine owns the external pin
  static PulseEngine s_engine;
#endif
  static uint16_t s_click_pulse_ms = 20;
  static uint8_t  s_click_mode     = PulseEngine::MODE_PULSE;
  static uint8_t  s_click_fade     = 3;

  enum : uint8_t { DO_CLICK, DO_ACTIVITY, DO_ACK, DO_DISP_ON, DO_DISP_OFF, DO_ON, DO_OFF, DO_ATTACH };
  static volatile uint8_t s_todo[8];
  volatile uint8_t s_head = 0;   // loop writes
  volatile uint8_t s_tail = 0;   // poll writes
  bool s_busy = false;

  // full drops it
  static void post(uint8_t what) {
    uint8_t h = s_head;
    if ((uint8_t)(h - s_tail) >= sizeof(s_todo)) return;
    s_todo[h % sizeof(s_todo)] = what;
    s_head = h + 1;
  }

  static void run(uint8_t what) {
    switch (what) {
      case DO_CLICK:
#ifdef BLIP_LED_PIN
        s_engine.start();
#else
        if (s_onboard.isRunning()) break;
        if (s_click_mode == PulseEngine::MODE_FADE) s_onboard.fade(s_click_fade);
        else s_onboard.pulse(s_click_pulse_ms);
#endif
        break;
      case DO_ACTIVITY: s_onboard.blink(500, 500); break;
      case DO_ACK:      s_onboard.pulse(200); break;
      // two blinks = auto off, long = always on
      case DO_DISP_ON:  s_onboard.blinkN(200, 100, 2); break;
      case DO_DISP_OFF: s_onboard.pulse(600); break;
      case DO_ON:       s_onboard.on(); break;
      case DO_ATTACH:   s_onboard.attach((uint8_t)s_pin, s_low); break;
      default:          s_onboard.off(); break;
    }
  }

  void use_pin(int pin, bool low_active) {
    s_pin = pin < 0 ? -1 : (int8_t)pin;
    s_low = low_active;
  }

  // after the modules, a pin one took stays theirs
  void attach() {
    if (s_pin < 0) return;
    if (const char* why = PinSafety::claim_output(s_pin, PSTR("LED"))) {
      Log::console(PSTR("LED: pin=%d unsafe (%s) - off"), s_pin, why);
      s_pin = -1;
      return;
    }
    post(DO_ATTACH);
  }

  void begin() {
    EGLed::setClock(fast_millis);
#ifdef BLIP_LED_PIN
    s_engine.pin      = BLIP_LED_PIN;
    s_engine.polarity = 0;
    s_engine.commitConfig();
    s_engine.begin();
#endif
  }

  void poll(uint32_t now_us) {
    while (s_tail != s_head) {
      uint8_t t = s_tail;
      run(s_todo[t % sizeof(s_todo)]);
      s_tail = t + 1;
    }
    s_onboard.update();
#ifdef BLIP_LED_PIN
    s_engine.loop(now_us);
    s_busy = s_onboard.isRunning() || s_engine.busy();
#else
    s_busy = s_onboard.isRunning();
#endif
  }

  void off() { post(DO_OFF); }
  void lit(bool on) { post(on ? DO_ON : DO_OFF); }

  void click() {
#if defined(BLIP_LED_PIN)
    if (s_engine.take(fast_millis())) post(DO_CLICK);
#elif !defined(DISABLE_INTERNAL_BLIP)
    if (s_pin < 0) return;   // no LED, nothing per count
    post(DO_CLICK);
#endif
  }

  void activity()        { post(DO_ACTIVITY); }
  void shortPressAck()   { post(DO_ACK); }
  void displayEnabled()  { post(DO_DISP_ON); }
  void displayDisabled() { post(DO_DISP_OFF); }

  void setBrightness(uint8_t level) {
    s_onboard.setBrightness(level);   // 8-bit
#ifdef BLIP_LED_PIN
    s_engine.active_level = PulseEngine::levelFrom8bit(level);
#endif
  }

  void setBlipEngineConfig(uint8_t engine_mode, uint16_t pulse_us,
                           uint8_t fade_shift_idx, uint8_t max_hz) {
#ifdef BLIP_LED_PIN
    s_engine.mode       = engine_mode;
    s_engine.pulse_us   = pulse_us;
    s_engine.fade_shift = (uint8_t)(fade_shift_idx + 2);
    s_engine.max_hz     = max_hz;
    s_engine.commitConfig();
#else
    (void)max_hz;
#endif
    // onboard led, same limits
    if (fade_shift_idx > 2) fade_shift_idx = 1;
    s_click_pulse_ms = (uint16_t)(constrain((uint32_t)pulse_us, PulseEngine::PULSE_US_MIN,
                                            PulseEngine::PULSE_US_MAX) / 1000);
    s_click_mode     = engine_mode;
    s_click_fade     = (uint8_t)(fade_shift_idx + 2);
  }

}
