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

#include "../Util/PulseEngine.h"

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
    bool          _enabled = false;
    PulseEngine   _engine;

    // Pulse Out has its own quiet window separate from Blip LED + Audio.
    int16_t       _q_from_min = -1;
    int16_t       _q_to_min   = -1;
    bool          isQuietNow();

};

extern PulseOut pulseout;

#endif
#endif
