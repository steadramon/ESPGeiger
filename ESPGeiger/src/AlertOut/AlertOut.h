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

#include <Arduino.h>
#include "../Module/EGModule.h"

class AlertOut : public EGModule {
  public:
    AlertOut() {}
    const char* name() override { return "alertout"; }
    uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
    uint8_t display_order() override { return 18; }
    uint16_t warmup_seconds() override { return 0; }
    bool has_tick() override { return true; }
    void begin() override;
    void s_tick(unsigned long now_s) override;
    const EGPrefGroup* prefs_group() override;
    void on_prefs_loaded() override;

  private:
    void apply_level(bool active);

    int8_t   _pin       = -1;
    uint8_t  _invert    = 0;     // 0 = active-high, 1 = active-low
    uint8_t  _mode      = 0;     // 0 = steady level, 1 = 1 Hz pulse while alerting
    bool     _last_state = false;
};

extern AlertOut alertout;

#endif
#endif
