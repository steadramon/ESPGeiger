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
#include <stddef.h>
#include "Corium/Platform/Flash.h"
#include "GeigerInput/GeigerInput.h"
#include "Corium/Net/HttpPoster.h"
#include "Corium/Prefs/Prefs.h"

#ifndef GMC_INTERVAL
#define GMC_INTERVAL 300
#endif

// %s not %f, soft-float is slow
const char GMC_URI[] PROGMEM = "http://www.gmcmap.com/log2.asp?AID=%s&GID=%s&CPM=%d&ACPM=%s&uSV=%s";

class GMC : public corium::HttpPoster {
  public:
    GMC();
    const char* name() override { return PSTR("gmc"); }
    void onPrefsLoaded() override;
    const corium::PrefGroup* prefsGroup() override;
    corium::LegacyRows legacyAliases() override;  // LEGACY IMPORT (remove in v1.1.0)
  protected:
    bool prepare(char* url, size_t cap, const char** body) override;
    bool interpret(const char* reply) override;
    const char* logTag() override { return PSTR("GMC"); }
};

extern GMC gmc;

#endif
#endif
