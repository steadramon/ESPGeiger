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
#include "../Util/Globals.h"
#include "../Util/DeviceInfo.h"
#include "../Counter/Counter.h"
#include "../Module/EGHttpPoster.h"
#include "../Prefs/EGPrefs.h"


extern Counter gcounter;

#ifndef GMC_INTERVAL
#define GMC_INTERVAL 300
#endif

// ACPM / uSV interpolated as pre-formatted strings (format_f). %.2f / %.4f
// via snprintf pull in soft-float on ESP8266 (~0.5-1ms per call).
const char GMC_URI[] PROGMEM = "http://www.gmcmap.com/log2.asp?AID=%s&GID=%s&CPM=%d&ACPM=%s&uSV=%s";

class GMC : public EGHttpPoster {
  public:
    GMC();
    const char* name() override { return "gmc"; }
    void on_prefs_loaded() override;
    const EGPrefGroup* prefs_group() override;
    const EGLegacyAlias* legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
  protected:
    bool prepare(char* url, size_t cap, const char** body) override;
    bool interpret(const char* reply) override;
    const char* log_tag() override { return "GMC"; }
};

extern GMC gmc;

#endif
#endif
