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
#include <stdint.h>
#include <stddef.h>
#include "Corium/Platform/Flash.h"
#include "GeigerInput/GeigerInput.h"
#include "Corium/Net/HttpPoster.h"
#include "Corium/Prefs/Prefs.h"

// s between uploads, pref settable
#ifndef RADMON_INTERVAL
#define RADMON_INTERVAL 60
#endif

#define RADMON_INTERVAL_MIN 30
#define RADMON_INTERVAL_MAX 1800

const char RADMON_URI[] PROGMEM = "http://radmon.org/radmon.php?function=submit&user=%s&password=%s&value=%s&unit=CPM";

class Radmon : public corium::HttpPoster {
  public:
    Radmon();
    const char* name() override { return PSTR("radmon"); }
    void onPrefsLoaded() override;
    const corium::PrefGroup* prefsGroup() override;
    uint8_t displayOrder() override { return 30; }
    corium::LegacyRows legacyAliases() override;
    void setInterval(int interval);
    int getInterval();
  protected:
    bool prepare(char* url, size_t cap, const char** body) override;
    bool interpret(const char* reply) override;
    const char* logTag() override { return PSTR("Radmon"); }
  private:
    uint16_t _interval_s = RADMON_INTERVAL;
};

extern Radmon radmon;

#endif
#endif
