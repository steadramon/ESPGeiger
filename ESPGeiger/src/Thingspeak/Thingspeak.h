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
#ifndef THINGSPEAK_H
#define THINGSPEAK_H
#ifdef THINGSPEAKOUT
#include <Arduino.h>
#include "../Status.h"
#include "../Util/DeviceInfo.h"
#include "../Counter/Counter.h"
#include "../Module/EGModule.h"
#include "../Prefs/EGPrefs.h"
#include "AsyncHTTPRequest_Generic.hpp"


extern Status status;
extern Counter gcounter;

#ifndef THINGSPEAK_INTERVAL
#define THINGSPEAK_INTERVAL 90
#endif

const char TS_URI[] PROGMEM = "http://api.thingspeak.com/update?api_key=%s&field1=%d&field2=%s&field3=%d&field4=%d";

class Thingspeak : public EGModule {
  public:
    Thingspeak();
    const char* name() override { return "thgspk"; }
    bool requires_wifi() override { return true; }
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return 500; }
    void loop(unsigned long now) override;
    const EGPrefGroup* prefs_group() override;
    size_t status_json(char* buf, size_t cap, unsigned long now) override;
    const EGLegacyAlias* legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
    void postMeasurement();
    AsyncHTTPRequest request;
    bool last_ok = false;
    unsigned long last_attempt_ms = 0;
  private:
    unsigned long lastPing = 0;
    static constexpr uint32_t pingIntervalMs = (uint32_t)THINGSPEAK_INTERVAL * 1000UL;
    static void httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState);
};

extern Thingspeak thingspeak;

#endif
#endif
