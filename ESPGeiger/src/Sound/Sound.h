/*
  Sound.h - buzzer sounds for events

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
#ifndef SOUND_H
#define SOUND_H
#ifdef GEIGER_BUZZER

#include <stdint.h>
#include "EGTune.h"
#include "Corium/Platform/Flash.h"
#include "Corium/Module/Module.h"

class Sound : public corium::Module {
  public:
    Sound() {}
    const char* name() override { return PSTR("sound"); }
    uint8_t displayOrder() override { return 19; }
    bool runsDuringOta() override { return true; }
    void preWifi() override;
    void begin() override;
    void loop(unsigned long now) override;
    const corium::PrefGroup* prefsGroup() override;
    void onPrefsLoaded() override;

    // per count, nothing while a tune plays
    void click(uint32_t now_us);
    // console: a sound's name or a tune
    void test(const char* v);
    // before begin, plays it through
    void setup_portal();

    enum : uint8_t { STARTUP, SETUP, WIFI, ALERT, ALERT_END, HVFAULT, SD_ERROR, OTA, OTA_FAIL, BUTTON, CLICK, COUNT };

  private:
    void start();
    void load();
    void events(uint16_t ev, unsigned long now);
    void wifi();
    void alarm(unsigned long now);
    void play(uint8_t id);
    const char* text(uint8_t id, uint16_t& len);
    void schedule(unsigned long now, uint32_t us);

    EGTune::Step _click = {};
    uint32_t _repeat_ms = 0;
    unsigned long _repeat_at = 0;
    uint16_t _bad = 0;   // bit per sound: its pref didn't parse
    uint8_t _duty = 25;
    uint8_t _vol = 100;
    volatile bool _reload = true;
    bool _click_on = false;
    bool _chimes = true;
    bool _started = false;
    bool _alert = false;
    bool _wifi = false;
    bool _fault_said = false;
};

extern Sound sound;

#endif
#endif
