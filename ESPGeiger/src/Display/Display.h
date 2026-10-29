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
#include "Corium/Measure/EGMeasure.h"
#include "Counter/CounterMeasures.h"
#include "Corium/Module/EGModule.h"
#include "Corium/Module/EGModuleRegistry.h"
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

class Display : public EGModule {
  public:
    const char* name() override { return PSTR("disp"); }
    uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
    uint8_t display_order() override { return 17; }
    bool hw_present() override { return backend()->present(); }
    const EGPrefGroup* prefs_group() override;
    const EGPrefShow* prefs_show() override;
    EGLegacyRows legacy_aliases() override;
    void on_prefs_loaded() override;
    void on_prefs_saved() override;
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
#endif
    void keep_on()                    { _auto_off = false; }
    void wake(unsigned long now) {
      _woke.mark(now);
      if (!_lit) refresh(now);
    }

    enum Sleep : uint8_t { AWAKE, TIMED_OUT, OFF_HOURS };
    Sleep sleep(unsigned long now);
    // looked at on new counts, taps and wakes
    void loop(unsigned long now) override { refresh(now); }
    // lit or dark changed, from loop()
    static constexpr uint8_t TOPIC_POWER = Topic::POWER;
    bool lit() const { return _lit; }

    // core rows, found at the first prefs load
    float cpm()       { return f(_cpm); }
    float cpm5()      { return f(_cpm5); }
    float cpm15()     { return f(_cpm15); }
    float cps()       { return f(_cps); }
    float usv()       { return f(_usv); }
    float ratio()     { return f(_ratio); }
    bool  warm()      { return b(_warm); }
    bool  no_counts() { return b(_none); }
    bool  alert()     { return b(_alert); }
    uint32_t blip() {
      EGMValue v;
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
      EGModuleRegistry::emit(TOPIC_POWER);
    }
    bool _lit = true;
    uint8_t _taps = 0;
    unsigned long _tap_ms = 0;
    void find_rows() {
      _cpm   = egm_core_read(PSTR("cpm"),       EGM_F32);
      _cpm5  = egm_core_read(PSTR("cpm5"),      EGM_F32);
      _cpm15 = egm_core_read(PSTR("cpm15"),     EGM_F32);
      _cps   = egm_core_read(PSTR("cps"),       EGM_F32);
      _usv   = egm_core_read(PSTR("usv"),       EGM_F32);
      _ratio = egm_core_read(PSTR("ratio"),     EGM_F32);
      _warm  = egm_core_read(PSTR("warm"),      EGM_BOOL);
      _none  = egm_core_read(PSTR("no_counts"), EGM_BOOL);
      _alert = egm_core_read(PSTR("alert"),     EGM_BOOL);
      _blip  = egm_core_read(PSTR("blip"),      EGM_U32);
    }
    static float f(EGMRead r) {
      EGMValue v;
      return (r && r(&v)) ? v.f : NAN;
    }
    static bool b(EGMRead r) {
      EGMValue v;
      return r && r(&v) && v.b;
    }
    EGMRead _cpm = nullptr, _cpm5 = nullptr, _cpm15 = nullptr, _cps = nullptr, _usv = nullptr;
    EGMRead _ratio = nullptr, _warm = nullptr, _none = nullptr, _alert = nullptr, _blip = nullptr;

    bool on_hours();
    bool _started = false;
    bool _auto_off = true;
    Stamp _woke;
    uint32_t _timeout_ms = 0;   // 0 = use on/off hours
};

extern Display display;

extern const char W_APP_NAME[] PROGMEM;   // splash screens

#endif
#endif
