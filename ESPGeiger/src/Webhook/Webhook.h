/*
  Webhook.h - Webhook class

  Copyright (C) 2025 @steadramon

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
#ifndef WEBHOOK_H
#define WEBHOOK_H
#include <Arduino.h>
#include "../Status.h"
#include "../Util/DeviceInfo.h"
#include "../Counter/Counter.h"
#include "../Module/EGModule.h"
#include "../Prefs/EGPrefs.h"
#include "AsyncHTTPRequest_Generic.hpp"


extern Status status;
extern Counter gcounter;

#ifndef WEBHOOK_INTERVAL_MIN
#define WEBHOOK_INTERVAL_MIN 10
#endif
#ifndef WEBHOOK_INTERVAL_MAX
#define WEBHOOK_INTERVAL_MAX 3600
#endif
#ifndef WEBHOOK_INTERVAL
#define WEBHOOK_INTERVAL 60
#endif

class Webhook : public EGModule {
  public:
    Webhook();
    const char* name() override { return "whook"; }
    bool requires_wifi() override { return true; }
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return 500; }
    void loop(unsigned long now) override;
    const EGPrefGroup* prefs_group() override;
    const EGLegacyAlias* legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
    void postMeasurement();
    void setInterval(int interval);
    int getInterval() { return pingInterval; }
    const char* cleanHTTP(const char* url);
    AsyncHTTPRequest request;
    bool last_ok = false;
    unsigned long last_attempt_ms = 0;
  private:
    unsigned long lastPing = 0;
    uint16_t pingInterval = WEBHOOK_INTERVAL;
    uint32_t pingIntervalMs = (uint32_t)WEBHOOK_INTERVAL * 1000UL;  // precomputed
    static void httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState);
};

extern Webhook webhook;

#endif
