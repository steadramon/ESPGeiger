/*
  SystemPrefs.h - The device's own prefs: system and network.

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
#ifndef CORIUM_SYSTEMPREFS_H
#define CORIUM_SYSTEMPREFS_H

#include "Corium/Module/Module.h"
#include "Prefs.h"

#define WEB_PASS_MAX 32
#define WEB_PASS_MIN 8    // for a new one, blank is still ok
#define WEB_PASS_SLOT 1   // KeyStore slot, 0 is the key

namespace corium {
class SystemPrefs : public Module {
  public:
    const char* name() override { return PSTR("sys"); }
    bool coreModule() override { return true; }
    uint8_t displayOrder() override { return 10; }
    const PrefGroup* prefsGroup() override;
    void onPrefsLoaded() override;
    void onPrefsSaved() override;
    void begin() override;
    void loop(unsigned long now) override;
    static bool passRestored;   // FS was lost, web_pass came back from the key store
    // kept must hold WEB_PASS_MAX + 1
    static const char* webPass(char* kept);
};
}  // namespace corium

extern corium::SystemPrefs sysprefs;

#endif
