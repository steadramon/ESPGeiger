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

#ifndef CORIUM_ARDUINO_OTA_H
#define CORIUM_ARDUINO_OTA_H

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

// how often handle() is polled
#ifndef OTA_POLL_INTERVAL_MS
#define OTA_POLL_INTERVAL_MS 100
#endif

namespace corium {
class ArduinoOTAModule : public Module {
  public:
    const char* name() override { return PSTR("ota"); }
    bool requiresWifi() override { return true; }
    bool coreModule() override { return true; }
    void begin() override;
    void loop(unsigned long now) override;
    bool runsDuringOta() override { return true; }
};
}  // namespace corium

extern corium::ArduinoOTAModule ota;

#endif
