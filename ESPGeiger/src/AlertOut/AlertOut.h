/*
  AlertOut.h - GPIO output mirroring the Counter alert state.

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
#ifndef ALERTOUT_H
#define ALERTOUT_H
#ifdef ALERT_OUT

#include <stdint.h>
#include "Corium/Platform/Flash.h"
#include "Corium/Module/EGModule.h"

class AlertOut : public EGModule {
  public:
    AlertOut() {}
    const char* name() override { return PSTR("alertout"); }
    uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
    uint8_t display_order() override { return 18; }
    void begin() override;
    void loop(unsigned long now) override;
    const EGPrefGroup* prefs_group() override;
    void on_prefs_loaded() override;

  private:
    void apply_level(bool active);

    static constexpr uint16_t FLIP_MS = 1000;   // pulse mode level flips
    int8_t   _pin       = -1;
    uint8_t  _invert    = 0;     // 0 = active-high, 1 = active-low
    uint8_t  _mode      = 0;     // 0 = steady, 1 = 1 Hz pulse
    bool     _pulsing   = false;
    bool     _high      = false;
    uint32_t _flip_at   = 0;
};

extern AlertOut alertout;

#endif
#endif
