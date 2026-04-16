/*
  GMC.h - GMC class

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

#ifndef GMC_H
#define GMC_H
#ifdef GMCOUT
#include <Arduino.h>
#include "../ConfigManager/ConfigManager.h"
#include "../Status.h"
#include "../Counter/Counter.h"
#include "../Module/EGModule.h"
#include "AsyncHTTPRequest_Generic.hpp"

#ifdef ESP8266
#include "ESP8266WiFi.h"
#elif defined(ESP32)
#include <WiFi.h>
#endif

extern Status status;
extern Counter gcounter;

#ifndef GMC_INTERVAL
#define GMC_INTERVAL 300
#endif

// ACPM / uSV interpolated as pre-formatted strings (format_f). %.2f / %.4f
// via snprintf pull in soft-float on ESP8266 (~0.5-1ms per call).
const char GMC_URI[] PROGMEM = "http://www.gmcmap.com/log2.asp?AID=%s&GID=%s&CPM=%d&ACPM=%s&uSV=%s";

class GMC : public EGModule {
  public:
    GMC();
    const char* name() override { return "gmc"; }
    bool requires_wifi() override { return true; }
    bool has_tick() override { return true; }
    void s_tick(unsigned long stick_now) override;
    void postMeasurement();
    AsyncHTTPRequest request;
    bool last_ok = false;
    unsigned long last_attempt_ms = 0;
  private:
    unsigned long lastPing = 0;
    const int pingInterval = 1000 * GMC_INTERVAL;
    static void httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState);
};

extern GMC gmc;

#endif
#endif
