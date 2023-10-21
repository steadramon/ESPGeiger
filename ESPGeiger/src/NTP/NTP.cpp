/*
  NTP.cpp - functions to handle NTP
  
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
#include <Arduino.h>
#include "NTP.h"
#include "../Logger/Logger.h"
#include <FS.h>
#include <LittleFS.h>
#include "ArduinoJson.h"
#ifdef ESP32
#include "sntp.h"
#endif

NTP_Client::NTP_Client() {
}

void NTP_Client::setup()
{
#ifndef DISABLE_NTP
  if (_ntp_server && !_ntp_server[0]) {
    memcpy(_ntp_server, NTP_SERVER, 64);
  }

  Log::console(PSTR("NTP: Starting ... %s"), _ntp_server);
  const char *possixTZ = getPosixTZforOlson(_ntp_tz);
#ifdef ESP8266
  settimeofday_cb([](){
    if (!status.ntp_synced) {
      Log::console(PSTR("NTP: Synched"));
      status.ntp_synced = true;
    }
  });

  configTime(possixTZ, _ntp_server);
#else
  sntp_set_time_sync_notification_cb([](struct timeval *t){
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)){
      return;
    }
    if (!status.ntp_synced) {
      Log::console(PSTR("NTP: Synched"));
      status.ntp_synced = true;
    }
  });
  configTime(0, 0, _ntp_server); // 0, 0 because we will use TZ in the next line
  setenv("TZ", possixTZ, 1); // Set environment variable with your time zone
  tzset();
#endif
#endif
}