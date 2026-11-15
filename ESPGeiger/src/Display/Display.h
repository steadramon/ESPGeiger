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
#include <math.h>
#include "Corium/Platform/Flash.h"
#include "DisplayBackend.h"
#include "Corium/Measure/Measure.h"
#include "Counter/CounterMeasures.h"
#include "Corium/Module/Module.h"
#include "Corium/Module/Registry.h"
#include "App/Topics.h"
#include "Corium/System/FastMillis.h"

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

// Prefs, sleep and the button for whichever DisplayBackend the type pref picks
class Display : public corium::Module {
  public:
    const char* name() override { return PSTR("disp"); }
    uint8_t priority() override { return CORIUM_PRIORITY_HARDWARE; }
    uint8_t displayOrder() override { return 17; }
    bool hwPresent() override { return backend()->present(); }
    const corium::PrefGroup* prefsGroup() override;
    const corium::PrefShow* prefsShow() override;
    corium::LegacyRows legacyAliases() override;
    void onPrefsLoaded() override;
    void onPrefsSaved() override;
    void registerRoutes(EGHttpServer& http) override;

    DisplayBackend* backend();   // never null
    // from a backend's file, at static init
    static constexpr uint8_t BACKENDS_MAX = 2;
    static bool add(DisplayBackend* b);

    void button(unsigned long now, int8_t dir);
    void setup_wifi(const char* host) { backend()->setup_wifi(host); }
#ifdef GEIGER_PUSHBUTTON
    bool toggle_auto_off(unsigned long now);
    void offline()                    { backend()->offline(); }
    void wipe(int seconds)            { backend()->wipe(seconds); }
    void presses(unsigned long now);
#else
    void presses(unsigned long) {}
#endif
    void keep_on()                    { _auto_off = false; }
    void wake(unsigned long now) {
      _woke.mark(now);
      if (!_lit) refresh(now);
    }

    enum Sleep : uint8_t { AWAKE, TIMED_OUT, OFF_HOURS };
    Sleep sleep(unsigned long now);
    // looked at on new counts, presses, taps and wakes
    void loop(unsigned long now) override {
      presses(now);
      refresh(now);
    }
    // lit or dark changed, from loop()
    static constexpr uint8_t TOPIC_POWER = Topic::POWER;
    bool lit() const { return _lit; }

    // core rows, found at the first prefs load
    float cpm()       { return corium::m_read_f(_cpm); }
    float cpm5()      { return corium::m_read_f(_cpm5); }
    float cpm15()     { return corium::m_read_f(_cpm15); }
    float cps()       { return corium::m_read_f(_cps); }
    float usv()       { return corium::m_read_f(_usv); }
    float ratio()     { return corium::m_read_f(_ratio); }
    bool  warm()      { return corium::m_read_b(_warm); }
    bool  no_counts() { return corium::m_read_b(_none); }
    bool  alert()     { return corium::m_read_b(_alert); }
    bool  warn()      { return corium::m_read_b(_warn); }
    uint32_t blip() {
      corium::MValue v;
      return (_blip && _blip(&v)) ? v.u : 0;
    }
    // the 5 min avg under 10
    float shown_cpm() {
      float c = cpm();
      return c < 10.0f ? display_cpm(c, cpm5()) : c;
    }
    // a graph under 10 goes to tenths, each second with its 5 min avg; 10 = tenths
    static const uint8_t HIST_MAX = 64;
    int tenths(int* h, uint16_t n) {
      int lo = n ? h[0] : 10;
      for (uint16_t i = 1; i < n; i++) if (h[i] < lo) lo = h[i];
      if (lo >= 10 || n > HIST_MAX) return 1;
      uint16_t c5[HIST_MAX];
      int m = cpm5() < 10.0f && warm() ? (int)egm_cpm5_history(c5, n) : 0;
      display_hist_was(h, n, c5, m);
      return 10;
    }

  private:
    void refresh(unsigned long now) {
      bool lit = sleep(now) == AWAKE;
      if (lit == _lit) return;
      _lit = lit;
      corium::Registry::emit(TOPIC_POWER);
    }
    bool _lit = true;
    uint8_t _taps = 0;
    unsigned long _tap_ms = 0;
    void find_rows() {
      _cpm   = corium::m_core_read(PSTR("cpm"),       corium::M_F32);
      _cpm5  = corium::m_core_read(PSTR("cpm5"),      corium::M_F32);
      _cpm15 = corium::m_core_read(PSTR("cpm15"),     corium::M_F32);
      _cps   = corium::m_core_read(PSTR("cps"),       corium::M_F32);
      _usv   = corium::m_core_read(PSTR("usv"),       corium::M_F32);
      _ratio = corium::m_core_read(PSTR("ratio"),     corium::M_F32);
      _warm  = corium::m_core_read(PSTR("warm"),      corium::M_BOOL);
      _none  = corium::m_core_read(PSTR("no_counts"), corium::M_BOOL);
      _alert = corium::m_core_read(PSTR("alert"),     corium::M_BOOL);
      _warn  = corium::m_core_read(PSTR("warn"),      corium::M_BOOL);
      _blip  = corium::m_core_read(PSTR("blip"),      corium::M_U32);
    }
    corium::MRead _cpm = nullptr, _cpm5 = nullptr, _cpm15 = nullptr, _cps = nullptr, _usv = nullptr;
    corium::MRead _ratio = nullptr, _warm = nullptr, _none = nullptr, _alert = nullptr, _blip = nullptr;
    corium::MRead _warn = nullptr;

    bool on_hours();
    bool _started = false;
    bool _auto_off = true;
    corium::Stamp _woke;
    uint32_t _timeout_ms = 0;   // 0 = use on/off hours
};

extern Display display;

extern const char W_APP_NAME[] PROGMEM;   // splash screens

#endif
#endif
