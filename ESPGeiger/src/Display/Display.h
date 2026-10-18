/*
  Display.h - The one display module the firmware talks to.

  Copyright (C) 2026 @steadramon

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
#ifndef EG_DISPLAY_H
#define EG_DISPLAY_H
#ifdef SSD1306_DISPLAY
#include <Arduino.h>
#include "DisplayBackend.h"
#include "Corium/Module/EGModule.h"

class Display : public EGModule {
  public:
    const char* name() override { return PSTR("disp"); }
    uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
    uint16_t warmup_seconds() override { return 0; }
    uint8_t display_order() override { return 17; }
    bool hw_present() override { return backend()->present(); }
    const EGPrefGroup* prefs_group() override;
    const EGPrefShow* prefs_show() override;
    EGLegacyRows legacy_aliases() override;
    void on_prefs_loaded() override;
    void on_prefs_saved() override;
    void registerRoutes(EGHttpServer& http) override;

    DisplayBackend* backend();   // never null

    void button(unsigned long now, int8_t dir);
    void setup_wifi(const char* host) { backend()->setup_wifi(host); }
#ifdef GEIGER_PUSHBUTTON
    bool toggle_auto_off(unsigned long now);
    void offline()                    { backend()->offline(); }
    void wipe(int seconds)            { backend()->wipe(seconds); }
#endif
    void keep_on()                    { _auto_off = false; }
    void wake(unsigned long now)      { _woken = now; }

    enum Sleep : uint8_t { AWAKE, TIMED_OUT, OFF_HOURS };
    Sleep sleep(unsigned long now);

  private:
    bool on_hours();
    bool _started = false;
    bool _auto_off = true;
    unsigned long _woken = 0;
    uint32_t _timeout_ms = 0;   // 0 = use on/off hours
};

extern Display display;

#endif
#endif
