/*
  Radmon.h - Radmon class

  Copyright (C) 2023 @steadramon

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

#ifndef RADMON_H
#define RADMON_H
#ifdef RADMONOUT
#include <Arduino.h>
#include "../Corium/System/Globals.h"
#include "../Corium/System/DeviceInfo.h"
#include "../GeigerInput/GeigerInput.h"
#include "../Corium/Net/EGHttpPoster.h"
#include "../Corium/Prefs/EGPrefs.h"

#ifndef RADMON_INTERVAL
#define RADMON_INTERVAL 60
#endif

#define RADMON_INTERVAL_MIN 30
#define RADMON_INTERVAL_MAX 1800

const char RADMON_URI[] PROGMEM = "http://radmon.org/radmon.php?function=submit&user=%s&password=%s&value=%s&unit=CPM";

class Radmon : public EGHttpPoster {
  public:
    Radmon();
    const char* name() override { return PSTR("radmon"); }
    void on_prefs_loaded() override;
    const EGPrefGroup* prefs_group() override;
    uint8_t display_order() override { return 30; }
    EGLegacyRows legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
    void setInterval(int interval);
    int getInterval();
  protected:
    bool prepare(char* url, size_t cap, const char** body) override;
    bool interpret(const char* reply) override;
    const char* log_tag() override { return PSTR("Radmon"); }
  private:
    uint16_t pingInterval = RADMON_INTERVAL;
};

extern Radmon radmon;

#endif
#endif
