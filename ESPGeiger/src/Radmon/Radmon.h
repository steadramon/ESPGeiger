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
#include "../Util/Globals.h"
#include "../Util/DeviceInfo.h"
#include "../Counter/Counter.h"
#include "../Module/EGModule.h"
#include "../Prefs/EGPrefs.h"
#include "AsyncHTTPRequest_Generic.hpp"


extern Counter gcounter;

#ifndef RADMON_INTERVAL
#define RADMON_INTERVAL 60
#endif

#define RADMON_INTERVAL_MIN 30 
#define RADMON_INTERVAL_MAX 1800

const char RADMON_URI[] PROGMEM = "http://radmon.org/radmon.php?function=submit&user=%s&password=%s&value=%d&unit=CPM";

class Radmon : public EGModule {
  public:
    Radmon();
    const char* name() override { return "radmon"; }
    bool requires_wifi() override { return true; }
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return 500; }
    void loop(unsigned long now) override;
    const EGPrefGroup* prefs_group() override;
    uint8_t display_order() override { return 30; }
    size_t status_json(char* buf, size_t cap, unsigned long now) override;
    const EGLegacyAlias* legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
    void postMeasurement();
    void setInterval(int interval);
    int getInterval();
    AsyncHTTPRequest request;
  private:
    unsigned long lastPing = 0;
    uint16_t pingInterval = RADMON_INTERVAL;
    uint32_t pingIntervalMs = (uint32_t)RADMON_INTERVAL * 1000UL;  // precomputed - no mul per check
    static void httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState);
};

extern Radmon radmon;

#endif
#endif
