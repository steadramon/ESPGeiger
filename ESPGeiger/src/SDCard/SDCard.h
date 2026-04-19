/*
  SDCard.h - SDCard class

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
#ifndef _SDCARD_h
#define _SDCARD_h
#ifdef GEIGER_SDCARD
#include <SPI.h>
#include <SdFat.h>
#include "../Counter/Counter.h"
#include "../Module/EGModule.h"
#include "../Prefs/EGPrefs.h"

#ifndef GEIGER_SDCARD_CS
#define GEIGER_SDCARD_CS 16
#endif

extern Counter gcounter;
extern SdFat32* sd;

class SDCard : public EGModule {
  public:
    SDCard();
    const char* name() override { return "sdcard"; }
    uint8_t display_order() override { return 40; }
    bool requires_ntp() override { return true; }
    bool has_tick() override { return true; }
    void s_tick(unsigned long stick_now) override;
    void begin() override;
    const EGPrefGroup* prefs_group() override;
    void on_prefs_loaded() override;
    void deleteOldest();
  protected:
    File32 myDataFile;
  private:
    time_t lastWrittenMinute = 0;
    uint32_t openFileDay = 0;  // YYYYMMDD of currently-open file, 0 if closed
    uint8_t _sync_min = 1;
    uint8_t _unsynced_writes = 0;
    bool sdenabled = false;
};

extern SDCard sdcard;

#endif
#endif