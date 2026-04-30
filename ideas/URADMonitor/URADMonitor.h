/*
  URADMonitor.h - uRADMonitor open-data upload client

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

#ifndef URADMONITOR_H
#define URADMONITOR_H
#ifdef URADMONITOROUT
#include <Arduino.h>
#include "../Util/Globals.h"
#include "../Util/DeviceInfo.h"
#include "../Counter/Counter.h"
#include "../Module/EGModule.h"
#include "../Prefs/EGPrefs.h"
#include "AsyncHTTPRequest_Generic.hpp"

extern Counter gcounter;

#ifndef URADMON_INTERVAL
#define URADMON_INTERVAL 60
#endif

#define URADMON_INTERVAL_MIN 60
#define URADMON_INTERVAL_MAX 1800

#ifndef URADMON_URL
#define URADMON_URL "http://data.uradmonitor.com/api/v1/upload/exp/"
#endif

class URADMonitor : public EGModule {
  public:
    URADMonitor();
    const char* name() override { return "uradmon"; }
    bool requires_wifi() override { return true; }
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return 500; }
    void loop(unsigned long now) override;
    const EGPrefGroup* prefs_group() override;
    uint8_t display_order() override { return 35; }
    size_t status_json(char* buf, size_t cap, unsigned long now) override;
    void postMeasurement();
    void setInterval(int interval);
    int getInterval();
    AsyncHTTPRequest request;
  private:
    unsigned long lastPing = 0;
    uint16_t pingInterval = URADMON_INTERVAL;
    uint32_t pingIntervalMs = (uint32_t)URADMON_INTERVAL * 1000UL;
    static void httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState);
};

extern URADMonitor uradmon;

#endif
#endif
