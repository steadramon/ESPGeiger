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

#ifndef _ARDUINO_OTA_h
#define _ARDUINO_OTA_h

#include "Arduino.h"

#ifdef ESP8266
#include <ESP8266mDNS.h>
#else
#include <ESPmDNS.h>
#include <Update.h>
#endif
#include <ArduinoOTA.h>
#include "../Module/EGModule.h"

// Polling cadence in ms for the OTA UDP socket. Negotiation handshake
// spans seconds, so 100 ms is plenty responsive while saving CPU.
#ifndef OTA_POLL_INTERVAL_MS
#define OTA_POLL_INTERVAL_MS 100
#endif

static const char K_HTTP[]   PROGMEM = "http";
static const char K_TCP[]    PROGMEM = "tcp";
static const char K_GEIGER[] PROGMEM = "geiger";

class ArduinoOTAModule : public EGModule {
  public:
    const char* name() override { return "ota"; }
    bool requires_wifi() override { return true; }
    uint16_t warmup_seconds() override { return 0; }
    void begin() override;
    void loop(unsigned long now) override;
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return OTA_POLL_INTERVAL_MS; }
};

extern ArduinoOTAModule arduinoOTA;

// True mid-flash. EGModuleRegistry pauses everything else while set.
// Reset on OTA error so a failed negotiation isn't terminal.
extern volatile bool ota_in_progress;

#endif
