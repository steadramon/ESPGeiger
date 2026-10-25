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
#include "Corium/Logger/Logger.h"
#include "Corium/System/FastMillis.h"
#include "App/AppConfig.h"
#include "Corium/System/PinSafety.h"
#include "PulseEngine.h"

namespace LedSignal {

  // dark till attach(), the pin comes from led.pin
  static EGLed s_onboard(EGLed::NO_PIN);
  static int8_t s_pin = -1;   // none in safe mode, prefs never load
  static bool   s_low = false;

  // the app's, from begin()
  static const Pattern* s_rows = nullptr;
  static uint8_t s_rows_n = 0;

#ifdef LED_FLASH_ENGINE
  // PulseEngine owns the external pin
  static PulseEngine s_engine;
#else
  static bool s_flash_led = false;
#endif
  static uint16_t s_flash_pulse_ms = 20;
  static uint8_t  s_flash_mode     = PulseEngine::MODE_PULSE;
  static uint8_t  s_flash_fade     = PulseEngine::FADE_SHIFT_DEFAULT;

  // rows go in as is, these sit above them
  enum : uint8_t { DO_FLASH = 252, DO_ON, DO_OFF, DO_ATTACH };
  static uint8_t s_todo[8];   // head's release/acquire orders it
  std::atomic<uint8_t> s_head{0};   // loop writes
  std::atomic<uint8_t> s_tail{0};   // poll writes
  bool s_busy = false;

  // full drops it
  static void post(uint8_t what) {
    uint8_t h = s_head.load(std::memory_order_relaxed);
    if ((uint8_t)(h - s_tail.load(std::memory_order_acquire)) >= sizeof(s_todo)) return;
    s_todo[h % sizeof(s_todo)] = what;
    s_head.store(h + 1, std::memory_order_release);
  }

  static void run(uint8_t what) {
    switch (what) {
      case DO_FLASH:
#ifdef LED_FLASH_ENGINE
        s_engine.start();
#else
        if (s_onboard.isRunning()) break;
        if (s_flash_mode == PulseEngine::MODE_FADE) s_onboard.fade(s_flash_fade);
        else s_onboard.pulse(s_flash_pulse_ms);
#endif
        break;
      case DO_ON:       s_onboard.on(); break;
      case DO_OFF:      s_onboard.off(); break;
      case DO_ATTACH:   s_onboard.attach((uint8_t)s_pin, s_low); break;
      default: {
        Pattern p;
        memcpy_P(&p, &s_rows[what], sizeof(p));
        s_onboard.blinkN(p.on_ms, p.off_ms, p.count);
        break;
      }
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

  void begin(const Pattern* rows, uint8_t count, int flash_pin, bool flash_led) {
    s_rows   = rows;
    s_rows_n = count < DO_FLASH ? count : DO_FLASH;
#ifdef LED_FLASH_ENGINE
    (void)flash_led;
    s_engine.pin      = flash_pin < 0 ? -1 : (int8_t)flash_pin;
    s_engine.polarity = 0;
    s_engine.commitConfig();
    s_engine.begin();
#else
    (void)flash_pin;
    s_flash_led = flash_led;
#endif
  }

  void poll(uint32_t now_us) {
    uint8_t t = s_tail.load(std::memory_order_relaxed);
    while (t != s_head.load(std::memory_order_acquire)) {
      run(s_todo[t % sizeof(s_todo)]);
      t = t + 1;
      s_tail.store(t, std::memory_order_release);
    }
    s_onboard.update();
#ifdef LED_FLASH_ENGINE
    s_engine.loop(now_us);
    s_busy = s_onboard.isRunning() || s_engine.busy();
#else
    s_busy = s_onboard.isRunning();
#endif
  }

  void off() { post(DO_OFF); }
  void lit(bool on) { post(on ? DO_ON : DO_OFF); }

  void flash() {
#ifdef LED_FLASH_ENGINE
    if (s_engine.take(fast_millis())) post(DO_FLASH);
#else
    if (s_pin < 0 || !s_flash_led) return;   // no LED, no flash
    post(DO_FLASH);
#endif
  }

  void play(uint8_t row) {
    if (row < s_rows_n) post(row);
  }

  void setBrightness(uint8_t level) {
    s_onboard.setBrightness(level);   // 8-bit
#ifdef LED_FLASH_ENGINE
    s_engine.active_level = PulseEngine::levelFrom8bit(level);
#endif
  }

  void setFlashConfig(uint8_t engine_mode, uint16_t pulse_us,
                           uint8_t fade_shift_idx, uint8_t max_hz) {
#ifdef LED_FLASH_ENGINE
    s_engine.mode       = engine_mode;
    s_engine.pulse_us   = pulse_us;
    s_engine.fade_shift = PulseEngine::fadeShiftFromUi(fade_shift_idx);
    s_engine.max_hz     = max_hz;
    s_engine.commitConfig();
#else
    (void)max_hz;
#endif
    // onboard led, same limits
    if (fade_shift_idx > 2) fade_shift_idx = 1;
    s_flash_pulse_ms = (uint16_t)(clamp((uint32_t)pulse_us, PulseEngine::PULSE_US_MIN,
                                        PulseEngine::PULSE_US_MAX) / 1000);
    s_flash_mode     = engine_mode;
    s_flash_fade     = PulseEngine::fadeShiftFromUi(fade_shift_idx);
  }

}
