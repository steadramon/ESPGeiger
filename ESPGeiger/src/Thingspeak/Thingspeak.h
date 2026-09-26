/*
  Thingspeak.h - Thingspeak class

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
#include "../Corium/System/Globals.h"
#include "../Corium/System/DeviceInfo.h"
#include "../GeigerInput/GeigerInput.h"
#include "../Corium/Net/EGHttpPoster.h"
#include "../Corium/Prefs/EGPrefs.h"

#ifndef THINGSPEAK_INTERVAL
#define THINGSPEAK_INTERVAL 90
#endif

const char TS_URI[] PROGMEM = "http://api.thingspeak.com/update?api_key=%s&field1=%d&field2=%s&field3=%d&field4=%d";

class Thingspeak : public EGHttpPoster {
  public:
    Thingspeak();
    const char* name() override { return PSTR("thgspk"); }
    void on_prefs_loaded() override;
    const EGPrefGroup* prefs_group() override;
    EGLegacyRows legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
  protected:
    bool prepare(char* url, size_t cap, const char** body) override;
    bool interpret(const char* reply) override;
    const char* log_tag() override { return PSTR("Thingspeak"); }
    const char* status_key() override { return PSTR("thingspeak"); }
    uint16_t timeout_s() const override { return 5; }
    size_t reply_cap() const override { return 32; }
};

extern Thingspeak thingspeak;

#endif
#endif
