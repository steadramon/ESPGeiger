/*
  LedSignal.cpp - LED patterns over EGLed

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
#include "Corium/System/PinSafety.h"
#include "PulseEngine.h"

namespace corium {
namespace LedSignal {

  // Dark until attach(), the pin comes from led.pin
  static EGLed s_onboard(EGLed::NO_PIN);
  static int8_t s_pin = -1;   // none in safe mode, the prefs never load
  static bool   s_low = false;

  // App's pattern table, set by begin()
  static const Pattern* s_rows = nullptr;
  static uint8_t s_rows_n = 0;

#if CORIUM_LEDSIGNAL_FLASH
  static bool s_flash_led = false;
  static uint16_t s_flash_pulse_ms = 20;
  static uint8_t  s_flash_mode     = PulseEngine::MODE_PULSE;
  static uint8_t  s_flash_fade     = PulseEngine::FADE_SHIFT_DEFAULT;
#endif

  // Row numbers are queued as is, these codes sit above them
  enum : uint8_t { DO_FLASH = 252, DO_ON, DO_OFF, DO_ATTACH };
  static uint8_t s_todo[8];   // ordered by the release/acquire on s_head
  std::atomic<uint8_t> s_head{0};   // loop writes
  std::atomic<uint8_t> s_tail{0};   // poll writes
  bool s_busy = false;

  // Loop only, dropped if the queue is full
  // 8 divides 256, so h % 8 holds over the uint8_t wrap
  static void post(uint8_t what) {
    uint8_t h = s_head.load(std::memory_order_relaxed);
    if ((uint8_t)(h - s_tail.load(std::memory_order_acquire)) >= sizeof(s_todo)) return;
    s_todo[h % sizeof(s_todo)] = what;
    s_head.store(h + 1, std::memory_order_release);
  }

  static void run(uint8_t what) {
    switch (what) {
#if CORIUM_LEDSIGNAL_FLASH
      case DO_FLASH:
        if (s_onboard.isRunning()) break;
        if (s_flash_mode == PulseEngine::MODE_FADE) s_onboard.fade(s_flash_fade);
        else s_onboard.pulse(s_flash_pulse_ms);
        break;
#endif
      case DO_ON:       s_onboard.on(); break;
      case DO_OFF:      s_onboard.off(); break;
      case DO_ATTACH:   s_onboard.attach((uint8_t)s_pin, s_low); break;
      default: {
        Pattern p;
        memcpy_P(&p, &s_rows[what], sizeof(p));
        s_onboard.blinkN(p.onMs, p.offMs, p.count);
        break;
      }
    }
  }

  void usePin(int pin, bool low_active) {
    s_pin = pin < 0 ? -1 : (int8_t)pin;
    s_low = low_active;
  }

  // Runs after the modules, a pin one of them took stays theirs
  void attach() {
    if (s_pin < 0) return;
    if (const char* why = PinSafety::claimOutput(s_pin, PSTR("LED"))) {
      Log::console(PSTR("LED: pin=%d unsafe (%s) - off"), s_pin, why);
      s_pin = -1;
      return;
    }
    post(DO_ATTACH);
  }

  void begin(const Pattern* rows, uint8_t count, bool flash_led) {
    s_rows   = rows;
    s_rows_n = count < DO_FLASH ? count : DO_FLASH;
#if CORIUM_LEDSIGNAL_FLASH
    s_flash_led = flash_led;
#else
    (void)flash_led;
#endif
  }

  void poll() {
    uint8_t t = s_tail.load(std::memory_order_relaxed);
    while (t != s_head.load(std::memory_order_acquire)) {
      run(s_todo[t % sizeof(s_todo)]);
      t = t + 1;
      s_tail.store(t, std::memory_order_release);
    }
    s_onboard.update();
    s_busy = s_onboard.isRunning();
  }

  void off() { post(DO_OFF); }
  void lit(bool on) { post(on ? DO_ON : DO_OFF); }

#if CORIUM_LEDSIGNAL_FLASH
  void flash() {
    if (s_pin < 0 || !s_flash_led) return;
    post(DO_FLASH);
  }

  void setFlashConfig(uint8_t engine_mode, uint16_t pulse_us, uint8_t fade_shift_idx) {
    // onboard led uses PulseEngine's limits
    if (fade_shift_idx > PulseEngine::FADE_SHIFT_MAX - PulseEngine::FADE_SHIFT_MIN)
      fade_shift_idx = PulseEngine::FADE_SHIFT_DEFAULT - PulseEngine::FADE_SHIFT_MIN;
    s_flash_pulse_ms = (uint16_t)(clamp((uint32_t)pulse_us, PulseEngine::PULSE_US_MIN,
                                        PulseEngine::PULSE_US_MAX) / 1000);
    s_flash_mode     = engine_mode;
    s_flash_fade     = (uint8_t)(fade_shift_idx + PulseEngine::FADE_SHIFT_MIN);
  }
#endif

  void play(uint8_t row) {
    if (row < s_rows_n) post(row);
  }

  void setBrightness(uint8_t level) {
    s_onboard.setBrightness(level);   // 8-bit
  }

}
}  // namespace corium
