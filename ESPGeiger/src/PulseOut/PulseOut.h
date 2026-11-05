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

#include <stdint.h>
#include "Corium/Platform/Flash.h"
#include "Corium/Module/Module.h"

#include "App/ChipVoice.h"
#include "Corium/Output/PulseEngine.h"
#include "Corium/Output/QuietWindow.h"

class PulseOut : public corium::Module {
  public:
    PulseOut() {}
    const char* name() override { return PSTR("pulse"); }
    uint8_t priority() override { return CORIUM_PRIORITY_HARDWARE; }
    uint8_t displayOrder() override { return 14; }
    void begin() override;
    const corium::PrefGroup* prefsGroup() override;
    void onPrefsLoaded() override;

    // ms ticker owns the pin, clicks post s_req
    static volatile uint8_t s_req;
    static uint8_t s_seen;
    static bool s_busy;
    static inline bool due() { return s_req != s_seen || s_busy; }
    void poll(uint32_t now_us);

    void notifyClick(unsigned long now_ms);

  private:
    void read_pin(bool was_enabled);
    void apply_voice();

    bool          _enabled = false;
    bool          _begun = false;
    int8_t        _pin = -1;   // boot pref, msTicker owns the pin after begin
    corium::PulseEngine _engine;
    ChipBlip      _voice = {1.0f, 1.0f};   // set at begin

    corium::QuietWindow _quiet;   // own, not the Blip LED one

};

extern PulseOut pulseout;

#endif
#endif
