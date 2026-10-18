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
#include "Corium/Module/EGModule.h"

#include "Corium/Output/PulseEngine.h"
#include "Corium/Output/QuietWindow.h"

class PulseOut : public EGModule {
  public:
    PulseOut() {}
    const char* name() override { return PSTR("pulse"); }
    uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
    uint8_t display_order() override { return 14; }
    uint16_t warmup_seconds() override { return 0; }
    void begin() override;
    const EGPrefGroup* prefs_group() override;
    void on_prefs_loaded() override;

    // msTickerCB owns the pin, clicks post s_req
    static volatile uint8_t s_req;
    static uint8_t s_seen;
    static bool s_busy;
    static inline bool due() { return s_req != s_seen || s_busy; }
    void poll(uint32_t now_us);

    void notifyClick(unsigned long now_ms);

  private:
    void read_pin(bool was_enabled);

    bool          _enabled = false;
    bool          _begun = false;
    int8_t        _pin = -1;   // boot pref, msTicker owns the pin after begin
    PulseEngine   _engine;

    QuietWindow   _quiet;   // own, not the Blip LED one

};

extern PulseOut pulseout;

#endif
#endif
