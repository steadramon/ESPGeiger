/*
  PulseEngine.h - GPIO state machine for a pulse or LED pin.
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
#ifndef CORIUM_PULSE_ENGINE_H
#define CORIUM_PULSE_ENGINE_H

#include <Arduino.h>
#include "Corium/System/FastMillis.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/Util/TokenBucket.h"
#ifdef ESP32
#include <EGLed.h>
#endif

namespace corium {
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
  uint32_t pulseUs    = 5000;
  uint32_t freqHz     = 3500;
  uint32_t cycles     = 1;
  uint8_t  polarity   = 0;
  uint8_t  fadeShift = FADE_SHIFT_DEFAULT;
  uint32_t maxHz      = 20;
  uint16_t activeLevel = FULL;  // FULL = digitalWrite

  // Caches + runtime state
  uint32_t pulseUsEff     = 5000;
  uint32_t burstFreqEff = 3500;
  uint16_t tokenIntervalMs = 50;
  uint8_t  tokens          = 0;
  uint32_t lastTokenMs     = 0;
  uint8_t  phasesRemaining = 0;
  bool     pinHigh         = false;
  uint32_t nextUs          = 0;
  uint32_t periodUs        = 0;
  uint16_t brightness      = 0;

  void begin() {
    if (pin < 0) return;
    pinMode(pin, OUTPUT);
#ifdef ESP32
    // level the pin falls back to when tone() lets go
    digitalWrite(pin, polarity ? HIGH : LOW);
#endif
    writeIdle();
    pinHigh = false;
    commitConfig();
  }

  static constexpr uint32_t PULSE_US_MIN = 1000;   // 1 ms poll
  static constexpr uint32_t PULSE_US_MAX = 50000;
  static constexpr uint32_t FREQ_MIN = 1000;
  static constexpr uint32_t FREQ_MAX = 8000;
  static constexpr uint32_t CYCLES_MAX = 10;
  static constexpr uint32_t MAX_HZ_MAX = 200;
  static constexpr uint8_t  TOKENS_MAX = 5;
  static constexpr uint8_t  FADE_SHIFT_MIN = 2;
  static constexpr uint8_t  FADE_SHIFT_MAX = 4;
  static constexpr uint8_t  FADE_SHIFT_DEFAULT = 3;
  static constexpr uint32_t FADE_STEP_US = 5000;
  static constexpr uint16_t FADE_FLOOR = 8;   // below it, straight to off

  // callers pass prefs straight in
  void commitConfig() {
    if (mode > MODE_FADE) mode = MODE_PULSE;
    pulseUs = clamp(pulseUs, PULSE_US_MIN, PULSE_US_MAX);
    freqHz   = clamp(freqHz, FREQ_MIN, FREQ_MAX);
    cycles   = clamp<uint32_t>(cycles, 1, CYCLES_MAX);
    if (polarity > 1) polarity = 0;
    if (maxHz > MAX_HZ_MAX) maxHz = MAX_HZ_MAX;
    if (fadeShift < FADE_SHIFT_MIN || fadeShift > FADE_SHIFT_MAX) fadeShift = FADE_SHIFT_DEFAULT;
    pulseUsEff     = pulseUs;   // caller may scale after
    burstFreqEff = freqHz;
    tokenIntervalMs = maxHz ? (uint16_t)(1000 / maxHz) : 0;
  }

  // loop side, rate cap only, never the pin
  inline bool take(uint32_t now_ms) {
    if (pin < 0) return false;
    if (!tokenIntervalMs) return true;
    // full, no credit banked
    return takeToken(now_ms, lastTokenMs, tokens, tokenIntervalMs, TOKENS_MAX, true);
  }

  // poll side from here
  inline void start() {
    if (phasesRemaining == 0) fire();
  }

  inline bool busy() const {
    return phasesRemaining != 0 || (mode == MODE_FADE && brightness > 0);
  }

  // poll side, micros; one compare when idle
  inline void loop(uint32_t now_us) {
    if (mode == MODE_FADE && brightness > 0) {
      if (!reached(now_us, nextUs)) return;
      uint16_t delta = brightness >> fadeShift;
      if (delta == 0) delta = 1;
      brightness -= delta;
      if (brightness < FADE_FLOOR) {
        brightness = 0;
        analogWrite(pin, polarity ? FULL : 0);
        return;
      }
      analogWrite(pin, level(brightness));
      nextUs = now_us + FADE_STEP_US;
      return;
    }
    if (phasesRemaining == 0) return;
    if (!reached(now_us, nextUs)) return;
    pinHigh = !pinHigh;
    if (pinHigh) writeActive(); else writeIdle();
    phasesRemaining--;
    if (phasesRemaining == 0) return;
    nextUs = now_us + periodUs;
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
  inline int level(uint16_t v) const { return polarity ? FULL - v : v; }

  inline void fire() {
    if (mode == MODE_FADE) {
      brightness = activeLevel;
      analogWrite(pin, level(activeLevel));
      phasesRemaining = 0;
      nextUs = (uint32_t)micros() + FADE_STEP_US;
      return;
    }
    if (mode == MODE_BURST) {
      // tone() takes ms, round up, at least 1
      uint32_t duration_us = (uint32_t)cycles * 1000000UL / burstFreqEff;
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
      tone(pin, burstFreqEff, duration_ms);
      phasesRemaining = 0;
      return;
    }
    if (activeLevel >= FULL) {
      writeActive();
    } else {
      analogWrite(pin, level(activeLevel));
    }
    pinHigh = true;
    phasesRemaining = 1;
    periodUs = pulseUsEff;
    nextUs = (uint32_t)micros() + pulseUsEff;
  }
};
}  // namespace corium

#endif
