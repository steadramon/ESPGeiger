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

const char GMC_URI[] PROGMEM = "http://www.gmcmap.com/log2.asp?AID=%s&GID=%s&CPM=%d&ACPM=%d&uSV=%s";

class GMC {
  public:
    GMC();
    void loop();
    void postMeasurement();
    AsyncHTTPRequest request;
  private:
    unsigned long lastPing = 0;
    const unsigned long pingInterval = 1000 * 300;
    static void httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState);
};

#endif