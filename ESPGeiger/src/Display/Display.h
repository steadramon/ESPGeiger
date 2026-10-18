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
#include <stdint.h>
#include "Corium/Platform/Flash.h"
#include "DisplayBackend.h"
#include "Corium/Module/EGModule.h"

// under 10 the 5 min avg, unless 3 sigma off
inline bool use_cpm5(float cpm, float cpm5) {
  if (cpm >= 10.0f || cpm5 >= 10.0f) return false;
  float d = cpm - cpm5;
  return d * d < 9.0f * cpm5;
}

inline float display_cpm(float cpm, float cpm5) {
  return use_cpm5(cpm, cpm5) ? cpm5 : cpm;
}

// tenths, as the number was
inline void display_hist_was(int* h, int n, const uint16_t* c5, int m) {
  for (int i = 0; i < n; i++) {
    int j = i - (n - m);
    if (j >= 0 && h[i] < 10 && c5[j] < 100 && use_cpm5((float)h[i], c5[j] * 0.1f)) h[i] = c5[j];
    else h[i] *= 10;
  }
}

// k 10 = tenths
inline void display_hist(int* h, int n, float cpm5, int k) {
  bool low = cpm5 < 10.0f;
  if (!low && k == 1) return;
  int c5k = (int)(cpm5 * k + 0.5f);
  for (int i = 0; i < n; i++) {
    if (low && h[i] < 10 && use_cpm5((float)h[i], cpm5)) h[i] = c5k;
    else                                              h[i] *= k;
  }
}

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
