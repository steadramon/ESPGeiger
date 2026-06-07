/*
  PulseOut.h - Per-pulse GPIO output for piezo / speaker / LED / line-out.

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
#ifndef PULSEOUT_H
#define PULSEOUT_H
#ifdef PULSE_OUT

#include <Arduino.h>
#include "../Module/EGModule.h"

class PulseOut : public EGModule {
  public:
    PulseOut() {}
    const char* name() override { return "pulse"; }
    uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
    uint8_t display_order() override { return 14; }
    uint16_t warmup_seconds() override { return 0; }
    // 1 ms cadence is fine for the single-pulse mode (sub-ms pulses get
    // stretched up to 1 ms but the LED / piezo response is still sharp)
    // and the LED fade mode (5 ms step interval). Resonant-burst mode at
    // multi-kHz frequencies is limited to 1 ms transition resolution.
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return 1; }
    void begin() override;
    void loop(unsigned long now_ms) override;
    const EGPrefGroup* prefs_group() override;
    void on_prefs_loaded() override;

    // Called from Counter when a pulse arrives. Token-bucket-throttled so
    // a high-rate source can't keep launching new clicks on top of each
    // other before the previous one finishes.
    void notifyClick(unsigned long now_ms);

  private:
    void startClick();
    inline void writeIdle()   { digitalWrite(_pin, _polarity ? HIGH : LOW); }
    inline void writeActive() { digitalWrite(_pin, _polarity ? LOW  : HIGH); }

    bool          _enabled    = false;
    int8_t        _pin        = -1;
    uint8_t       _mode       = 0;       // 0 = single pulse, 1 = resonant burst, 2 = LED fade
    uint16_t      _pulse_us   = 500;
    uint16_t      _freq_hz    = 3500;
    uint8_t       _cycles     = 3;
    uint8_t       _polarity   = 0;       // 0 = active high, 1 = active low
    uint8_t       _fade_shift = 3;       // exponential decay: brightness -= brightness >> shift

    // Per-device voice variation from the chip ID. Subtly different sound
    // between adjacent units. Computed once in begin().
    float         _voice_pulse = 1.0f;
    float         _voice_freq  = 1.0f;

    // Click state machine. _phases_remaining counts down through the pin
    // toggles for the current click; 0 means idle. _pin_high tracks the
    // current logical level so loop() knows what to flip to. _brightness
    // is the live PWM value for the fade mode.
    uint8_t       _phases_remaining = 0;
    bool          _pin_high          = false;
    uint16_t      _brightness        = 0;       // 0..255, only used in fade mode
    uint32_t      _next_us           = 0;       // micros() target for next step
    uint32_t      _period_us         = 0;       // gap between steps

    unsigned long _last_emit_ms  = 0;
    uint8_t       _tokens        = 5;
    unsigned long _last_token_ms = 0;
};

extern PulseOut pulseout;

#endif
#endif
