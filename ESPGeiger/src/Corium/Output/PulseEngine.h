/*
  PulseEngine.h - GPIO state machine for Pulse Out / Blip LED.
  Three modes (Pulse, Burst, Fade) + token-bucket rate cap.
  Header-only for inlining on ESP8266.

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
#ifndef PULSE_ENGINE_H
#define PULSE_ENGINE_H

#include <Arduino.h>
#include "../GeigerInput/GeigerInput.h"

// ESP8266 Timer1 is owned by the fake pulse generator (test builds) or the
// HV booster PWM (ESPGeigerHW). tone() and analogWrite() both need Timer1, so
// Burst and Fade modes can't run there - fall back to plain Pulse.
#if defined(ESP8266) && (GEIGER_IS_TEST(GEIGER_TYPE) || defined(ESPGEIGER_HW))
#define EGPE_NO_PWM
#endif

class PulseEngine {
public:
  enum Mode : uint8_t { MODE_PULSE = 0, MODE_BURST = 1, MODE_FADE = 2 };

  // Config - owner writes from prefs then calls commitConfig().
  int8_t   pin        = -1;
  uint8_t  mode       = MODE_PULSE;
  uint16_t pulse_us   = 5000;
  uint16_t freq_hz    = 3500;
  uint16_t cycles     = 1;
  uint8_t  polarity   = 0;
  uint8_t  fade_shift = 3;
  uint8_t  max_hz     = 20;
  uint8_t  active_level = 255;  // 255 = digitalWrite, lower = analogWrite

  // Per-device chip-id jitter, optional.
  float    voice_pulse = 1.0f;
  float    voice_freq  = 1.0f;

  // Caches + runtime state.
  uint32_t pulse_us_eff   = 5000;
  uint32_t burst_freq_eff = 3500;
  uint16_t token_interval_ms = 50;
  uint8_t  tokens          = 0;
  unsigned long last_token_ms = 0;
  uint8_t  phases_remaining = 0;
  bool     pin_high        = false;
  uint32_t next_us         = 0;
  uint32_t period_us       = 0;
  uint8_t  brightness      = 0;

  void begin() {
    if (pin < 0) return;
    pinMode(pin, OUTPUT);
    writeIdle();
    pin_high = false;
    commitConfig();
  }

  void commitConfig() {
    pulse_us_eff   = (uint32_t)(pulse_us * voice_pulse);
    burst_freq_eff = (uint32_t)(freq_hz  * voice_freq);
    if (burst_freq_eff == 0) burst_freq_eff = 1;
    token_interval_ms = max_hz ? (uint16_t)(1000 / max_hz) : 0;
#ifdef EGPE_NO_PWM
    if (mode != MODE_PULSE) mode = MODE_PULSE;
#endif
  }

  // Returns true if this click fired. Caller passes now_ms (one clock read).
  inline bool notifyClick(unsigned long now_ms) {
    if (pin < 0) return false;
    if (token_interval_ms) {
      while ((now_ms - last_token_ms) >= token_interval_ms && tokens < 5) {
        last_token_ms += token_interval_ms;
        tokens++;
      }
      if ((now_ms - last_token_ms) > 5000) last_token_ms = now_ms;
      if (tokens == 0) return false;
      tokens--;
    }
    if (phases_remaining != 0) return false;
    startClick();
    return true;
  }

  // Per-tick state advance. Cheap when idle (single compare).
  inline void loop() {
#ifndef EGPE_NO_PWM
    if (mode == MODE_FADE && brightness > 0) {
      uint32_t now_us = (uint32_t)micros();
      if ((int32_t)(now_us - next_us) < 0) return;
      uint8_t delta = brightness >> fade_shift;
      if (delta == 0) delta = 1;
      brightness -= delta;
      if (brightness < 2) {
        brightness = 0;
        analogWrite(pin, polarity ? 255 : 0);
        return;
      }
      analogWrite(pin, polarity ? (255 - brightness) : brightness);
      next_us = now_us + 5000;
      return;
    }
#endif
    if (phases_remaining == 0) return;
    uint32_t now_us = (uint32_t)micros();
    if ((int32_t)(now_us - next_us) < 0) return;
    pin_high = !pin_high;
    if (pin_high) writeActive(); else writeIdle();
    phases_remaining--;
    if (phases_remaining == 0) return;
    next_us = now_us + period_us;
  }

private:
  inline void writeIdle()   { digitalWrite(pin, polarity ? HIGH : LOW); }
  inline void writeActive() { digitalWrite(pin, polarity ? LOW  : HIGH); }

  inline void startClick() {
    if (mode == MODE_FADE) {
      brightness = active_level;
      analogWrite(pin, polarity ? (255 - active_level) : active_level);
      phases_remaining = 0;
      next_us = (uint32_t)micros() + 5000;
      return;
    }
    if (mode == MODE_BURST) {
      uint32_t duration_us = (uint32_t)cycles * 1000000UL / burst_freq_eff;
      uint32_t duration_ms = (duration_us + 999u) / 1000u;
      if (duration_ms == 0) duration_ms = 1;
      tone(pin, burst_freq_eff, duration_ms);
      phases_remaining = 0;
      return;
    }
    if (active_level >= 255) {
      writeActive();
    } else {
      analogWrite(pin, polarity ? (255 - active_level) : active_level);
    }
    pin_high = true;
    phases_remaining = 1;
    period_us = pulse_us_eff;
    next_us = (uint32_t)micros() + pulse_us_eff;
  }
};

#endif
