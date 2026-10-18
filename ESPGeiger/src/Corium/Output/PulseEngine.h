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
#ifdef ESP32
#include <EGLed.h>
#endif

class PulseEngine {
public:
  enum Mode : uint8_t { MODE_PULSE = 0, MODE_BURST = 1, MODE_FADE = 2 };

  // analogWrite full scale, esp32 is 8-bit
#ifdef ESP8266
  static constexpr uint16_t FULL = 1023;
#else
  static constexpr uint16_t FULL = 255;
#endif

  // always convert through these
  static constexpr uint16_t levelFromPercent(uint32_t pct) {
    return (uint16_t)(((pct > 100 ? 100 : pct) * FULL + 50) / 100);
  }
  static constexpr uint16_t levelFrom8bit(uint32_t v) {
    return (uint16_t)(((v > 255 ? 255 : v) * FULL + 127) / 255);
  }

  // Config, then commitConfig()
  // wide so a bad pref clamps, not wraps
  int8_t   pin        = -1;
  uint8_t  mode       = MODE_PULSE;
  uint32_t pulse_us   = 5000;
  uint32_t freq_hz    = 3500;
  uint32_t cycles     = 1;
  uint8_t  polarity   = 0;
  uint8_t  fade_shift = FADE_SHIFT_DEFAULT;
  uint32_t max_hz     = 20;
  uint16_t active_level = FULL;  // FULL = digitalWrite

  float    voice_pulse = 1.0f;
  float    voice_freq  = 1.0f;

  // Caches + runtime state
  uint32_t pulse_us_eff   = 5000;
  uint32_t burst_freq_eff = 3500;
  uint16_t token_interval_ms = 50;
  uint8_t  tokens          = 0;
  uint32_t last_token_ms   = 0;
  uint8_t  phases_remaining = 0;
  bool     pin_high        = false;
  uint32_t next_us         = 0;
  uint32_t period_us       = 0;
  uint16_t brightness      = 0;

  void begin() {
    if (pin < 0) return;
    pinMode(pin, OUTPUT);
#ifdef ESP32
    // level the pin falls back to when tone() lets go
    digitalWrite(pin, polarity ? HIGH : LOW);
#endif
    writeIdle();
    pin_high = false;
    commitConfig();
  }

  static constexpr uint32_t PULSE_US_MIN = 1000;   // 1 ms poll
  static constexpr uint32_t PULSE_US_MAX = 50000;
  static constexpr uint32_t FREQ_MIN = 1000;
  static constexpr uint32_t FREQ_MAX = 8000;
  static constexpr uint32_t CYCLES_MAX = 10;
  static constexpr uint32_t MAX_HZ_MAX = 200;
  static constexpr uint8_t  FADE_SHIFT_MIN = 2;
  static constexpr uint8_t  FADE_SHIFT_MAX = 4;
  static constexpr uint8_t  FADE_SHIFT_DEFAULT = 3;

  // callers pass prefs straight in
  void commitConfig() {
    if (mode > MODE_FADE) mode = MODE_PULSE;
    if (pulse_us < PULSE_US_MIN) pulse_us = PULSE_US_MIN;
    if (pulse_us > PULSE_US_MAX) pulse_us = PULSE_US_MAX;
    if (freq_hz < FREQ_MIN) freq_hz = FREQ_MIN;
    if (freq_hz > FREQ_MAX) freq_hz = FREQ_MAX;
    if (cycles < 1) cycles = 1;
    if (cycles > CYCLES_MAX) cycles = CYCLES_MAX;
    if (polarity > 1) polarity = 0;
    if (max_hz > MAX_HZ_MAX) max_hz = MAX_HZ_MAX;
    if (fade_shift < FADE_SHIFT_MIN || fade_shift > FADE_SHIFT_MAX) fade_shift = FADE_SHIFT_DEFAULT;
    pulse_us_eff   = (uint32_t)(pulse_us * voice_pulse);
    burst_freq_eff = (uint32_t)(freq_hz  * voice_freq);
    if (burst_freq_eff == 0) burst_freq_eff = 1;
    token_interval_ms = max_hz ? (uint16_t)(1000 / max_hz) : 0;
  }

  // loop side, rate cap only, never the pin
  inline bool take(uint32_t now_ms) {
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
    return true;
  }

  // poll side from here
  inline void start() {
    if (phases_remaining == 0) startClick();
  }

  inline bool busy() const {
    return phases_remaining != 0 || (mode == MODE_FADE && brightness > 0);
  }

  // one compare when idle
  inline void loop(uint32_t now_us) {
    if (mode == MODE_FADE && brightness > 0) {
      if ((int32_t)(now_us - next_us) < 0) return;
      uint16_t delta = brightness >> fade_shift;
      if (delta == 0) delta = 1;
      brightness -= delta;
      if (brightness < 8) {
        brightness = 0;
        analogWrite(pin, polarity ? FULL : 0);
        return;
      }
      analogWrite(pin, polarity ? (FULL - brightness) : brightness);
      next_us = now_us + 5000;
      return;
    }
    if (phases_remaining == 0) return;
    if ((int32_t)(now_us - next_us) < 0) return;
    pin_high = !pin_high;
    if (pin_high) writeActive(); else writeIdle();
    phases_remaining--;
    if (phases_remaining == 0) return;
    next_us = now_us + period_us;
  }

private:
#ifdef ESP32
  // once ledc has the pin digitalWrite does nothing
  inline void writeIdle()   { analogWrite(pin, polarity ? FULL : 0); }
  inline void writeActive() { analogWrite(pin, polarity ? 0 : FULL); }
#else
  inline void writeIdle()   { digitalWrite(pin, polarity ? HIGH : LOW); }
  inline void writeActive() { digitalWrite(pin, polarity ? LOW  : HIGH); }
#endif

  inline void startClick() {
    if (mode == MODE_FADE) {
      brightness = active_level;
      analogWrite(pin, polarity ? (FULL - active_level) : active_level);
      phases_remaining = 0;
      next_us = (uint32_t)micros() + 5000;
      return;
    }
    if (mode == MODE_BURST) {
      uint32_t duration_us = (uint32_t)cycles * 1000000UL / burst_freq_eff;
      uint32_t duration_ms = (duration_us + 999u) / 1000u;
      if (duration_ms == 0) duration_ms = 1;
#ifdef ESP32
      // queued ahead of the tone, task only made on first burst
      static bool s_tone_chan = false;
      if (!s_tone_chan) {
        setToneChannel(EGLED_TONE_CHANNEL);
        s_tone_chan = true;
      }
#endif
      tone(pin, burst_freq_eff, duration_ms);
      phases_remaining = 0;
      return;
    }
    if (active_level >= FULL) {
      writeActive();
    } else {
      analogWrite(pin, polarity ? (FULL - active_level) : active_level);
    }
    pin_high = true;
    phases_remaining = 1;
    period_us = pulse_us_eff;
    next_us = (uint32_t)micros() + pulse_us_eff;
  }
};

#endif
