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
#include "Corium/System/IsrLock.h"
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

  // Always convert levels through these
  static constexpr uint16_t levelFromPercent(uint32_t pct) {
    return (uint16_t)(((pct > 100 ? 100 : pct) * FULL + 50) / 100);
  }
  static constexpr uint16_t levelFrom8bit(uint32_t v) {
    return (uint16_t)(((v > 255 ? 255 : v) * FULL + 127) / 255);
  }

  // Config. Set these then call commitConfig(), from the loop side
  // wide types so a bad pref gets clamped rather than wrapping
  int8_t   pin        = -1;
  uint8_t  mode       = MODE_PULSE;
  uint32_t pulseUs    = 5000;
  uint32_t freqHz     = 3500;
  uint32_t cycles     = 1;
  uint8_t  polarity   = 0;
  uint8_t  fadeShift  = FADE_SHIFT_DEFAULT;
  uint32_t maxHz      = 20;
  uint16_t activeLevel = FULL;  // FULL = digitalWrite

  // Caches + runtime state
  uint32_t pulseUsEff   = 5000;
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
    commitConfig();
    pinMode(pin, OUTPUT);
#ifdef ESP32
    // Attach once here, after that each edge only writes the duty
    analogWrite(pin, polarity ? FULL : 0);
    _chan = analogGetChannel(pin);
    _out = _chan;
#endif
    adopt();
    writeIdle();
    pinHigh = false;
  }

  static constexpr uint32_t PULSE_US_MIN = 1000;   // the poll runs every 1 ms
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
  static constexpr uint16_t FADE_FLOOR = 8;   // below this the fade goes straight to off

  // Clamps everything, so callers can pass prefs straight in
  void commitConfig() {
    if (mode > MODE_FADE) mode = MODE_PULSE;
    pulseUs  = clamp(pulseUs, PULSE_US_MIN, PULSE_US_MAX);
    freqHz   = clamp(freqHz, FREQ_MIN, FREQ_MAX);
    cycles   = clamp<uint32_t>(cycles, 1, CYCLES_MAX);
    if (polarity > 1) polarity = 0;
    if (maxHz > MAX_HZ_MAX) maxHz = MAX_HZ_MAX;
    if (fadeShift < FADE_SHIFT_MIN || fadeShift > FADE_SHIFT_MAX) fadeShift = FADE_SHIFT_DEFAULT;
    pulseUsEff     = pulseUs;   // the caller may scale it after, then call publish()
    burstFreqEff   = freqHz;
    tokenIntervalMs = maxHz ? (uint16_t)(1000 / maxHz) : 0;
    publish();
  }

  // The poll side picks this up at its next start()
  void publish() {
    IsrLock::enter(mux());
    _next = { mode, polarity, fadeShift, activeLevel, pulseUsEff, burstFreqEff, cycles };
    _gen++;
    IsrLock::leave(mux());
  }

  // Loop side. Only checks the rate cap, it never touches the pin
  inline bool take(uint32_t now_ms) {
    if (pin < 0) return false;
    // when the bucket is full its clock restarts, so no credit is banked
    return takeToken(now_ms, lastTokenMs, tokens, tokenIntervalMs, TOKENS_MAX, true);
  }

  // Everything from here runs on the poll side
  inline void start() {
    if (phasesRemaining != 0) return;
    adopt();
    fire();
  }

  inline bool busy() const {
    return phasesRemaining != 0 || (_run.mode == MODE_FADE && brightness > 0);
  }

  // Applies a saved polarity now. A running pulse picks it up when it ends
  inline void idleNow() {
    if (pin < 0 || busy()) return;
    adopt();
    writeIdle();
  }

  // poll side, in micros. Only one compare when idle
  inline void loop(uint32_t now_us) {
    if (_run.mode == MODE_FADE && brightness > 0) {
      if (!reached(now_us, nextUs)) return;
      uint16_t delta = brightness >> _run.fadeShift;
      if (delta == 0) delta = 1;
      brightness -= delta;
      if (brightness < FADE_FLOOR) {
        brightness = 0;
        adopt();
        writeLevel(_run.polarity ? FULL : 0);
        return;
      }
      writeLevel(level(brightness));
      nextUs = now_us + FADE_STEP_US;
      return;
    }
    if (phasesRemaining == 0) return;
    if (!reached(now_us, nextUs)) return;
    pinHigh = !pinHigh;
    if (pinHigh) {
      writeActive();
    } else {
      adopt();
      writeIdle();
    }
    phasesRemaining--;
    if (phasesRemaining == 0) return;
    nextUs = now_us + periodUs;
  }

private:
  // The settings fire() and loop() run with
  struct Run {
    uint8_t  mode;
    uint8_t  polarity;
    uint8_t  fadeShift;
    uint16_t level;
    uint32_t pulseUs;
    uint32_t burstHz;
    uint32_t cycles;
  };
  Run _next = {};   // begin() fills both before they're used
  Run _run = {};
  volatile uint8_t _gen = 0;
  uint8_t _seen = 0;

  // One lock shared by all engines, saves are rare
  static IsrLock::Mux& mux() {
    static IsrLock::Mux m;
    return m;
  }

  // only between pulses, never in the middle of one
  inline void adopt() {
    if (_gen == _seen) return;
    IsrLock::enter(mux());
    _run = _next;
    _seen = _gen;
    IsrLock::leave(mux());
#ifdef ESP32
    route();
#endif
  }

#ifdef ESP32
  static constexpr uint8_t  PWM_BITS   = 8;   // same as analogWrite
  static constexpr uint16_t BURST_DUTY = (FULL + 1) / 2;
  int8_t   _chan   = -1;   // analogWrite's channel, set in begin()
  int8_t   _out    = -1;   // channel the pin is attached to now
  uint32_t _toneHz = 0;    // 0 = pin on _chan

  // Bursts use the tone channel, which has its own timer. Only moves the pin on a mode or Hz change
  void route() {
    uint32_t hz = _run.mode == MODE_BURST ? _run.burstHz : 0;
    if (hz == _toneHz) return;
    _toneHz = hz;
    _out = hz ? EGLED_TONE_CHANNEL : _chan;
    if (hz) ledcSetup(_out, hz, PWM_BITS);
    ledcAttachPin(pin, _out);
    writeIdle();
  }

  inline void writeLevel(int v) { ledcWrite(_out, v); }
  inline void writeIdle()   { writeLevel(_run.polarity ? FULL : 0); }
  inline void writeActive() { writeLevel(_run.polarity ? 0 : FULL); }
  inline void startBurst()  { writeLevel(BURST_DUTY); }
#else
  inline void writeLevel(int v) { analogWrite(pin, v); }
  inline void writeIdle()   { digitalWrite(pin, _run.polarity ? HIGH : LOW); }
  inline void writeActive() { digitalWrite(pin, _run.polarity ? LOW  : HIGH); }
  inline void startBurst()  { tone(pin, _run.burstHz, 0); }   // 0 = until writeIdle
#endif
  inline int level(uint16_t v) const { return _run.polarity ? FULL - v : v; }

  inline void fire() {
    if (_run.mode == MODE_FADE) {
      brightness = _run.level;
      writeLevel(level(_run.level));
      phasesRemaining = 0;
      nextUs = (uint32_t)micros() + FADE_STEP_US;
      return;
    }
    uint32_t us = _run.pulseUs;
    if (_run.mode == MODE_BURST) {
      // The poll ends it, same as a pulse
      us = _run.cycles * 1000000UL / _run.burstHz;
      startBurst();
    } else if (_run.level >= FULL) {
      writeActive();
    } else {
      writeLevel(level(_run.level));
    }
    pinHigh = true;
    phasesRemaining = 1;
    periodUs = us;
    nextUs = (uint32_t)micros() + us;
  }
};
}  // namespace corium

#endif
