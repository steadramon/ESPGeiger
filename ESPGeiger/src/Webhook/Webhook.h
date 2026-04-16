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
    bool has_tick() override { return true; }
    void s_tick(unsigned long stick_now) override;
    void postMeasurement();
    void setInterval(int interval);
    int getInterval() { return pingInterval / 1000; }
    const char* cleanHTTP(const char* url);
    AsyncHTTPRequest request;
    bool last_ok = false;
    unsigned long last_attempt_ms = 0;
  private:
    unsigned long lastPing = 0;
    int pingInterval = 1000 * WEBHOOK_INTERVAL;
    static void httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState);
};

extern Webhook webhook;

#endif
