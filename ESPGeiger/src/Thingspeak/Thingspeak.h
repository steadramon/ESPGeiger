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
#include "../ConfigManager/ConfigManager.h"
#include "../Status.h"
#include "../Counter/Counter.h"
#include "AsyncHTTPRequest_Generic.hpp"

#ifdef ESP8266
#include "ESP8266WiFi.h"
#elif defined(ESP32)
#include <WiFi.h>
#endif

extern Status status;
extern Counter gcounter;

#ifndef THINGSPEAK_INTERVAL
#define THINGSPEAK_INTERVAL 90
#endif

const char TS_URI[] PROGMEM = "http://api.thingspeak.com/update?api_key=%s&field1=%d&field2=%s&field3=%d&field4=%d";

class Thingspeak {
  public:
    Thingspeak();
    void loop();
    void postMeasurement();
    AsyncHTTPRequest request;
  private:
    unsigned long lastPing = 0;
    const int pingInterval = 1000 * THINGSPEAK_INTERVAL;
    static void httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState);
};

#endif
#endif