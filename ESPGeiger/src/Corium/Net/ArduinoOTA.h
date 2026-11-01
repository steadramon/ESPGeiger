/*
  ArduinoOTA.h - functions to handle Arduino OTA Update

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

#ifndef ARDUINO_OTA_h
#define ARDUINO_OTA_h

#include <stdint.h>
#include "Corium/Platform/Flash.h"

#ifdef ESP8266
#include <ESP8266mDNS.h>
#else
#include <ESPmDNS.h>
#include <Update.h>
#endif
#include <ArduinoOTA.h>
#include "Corium/Module/Module.h"

#ifndef OTA_POLL_INTERVAL_MS
#define OTA_POLL_INTERVAL_MS 100
#endif

class ArduinoOTAModule : public corium::Module {
  public:
    const char* name() override { return PSTR("ota"); }
    bool requires_wifi() override { return true; }
    bool core_module() override { return true; }
    void begin() override;
    void loop(unsigned long now) override;
    bool runs_during_ota() override { return true; }
};

extern ArduinoOTAModule ota;

#endif
