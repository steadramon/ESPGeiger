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
#ifndef SDCARD_h
#define SDCARD_h
#ifdef GEIGER_SDCARD
#include <SPI.h>
#include <SdFat.h>
#include "Counter/Counter.h"
#include "Corium/Module/EGModule.h"
#include "Corium/Prefs/EGPrefs.h"

#ifndef GEIGER_SDCARD_CS
#define GEIGER_SDCARD_CS 16
#endif

extern SdFat32* sd;

class SDCard : public EGModule {
  public:
    SDCard();
    const char* name() override { return PSTR("sdcard"); }
    uint8_t display_order() override { return 20; }
    bool requires_ntp() override { return true; }
    bool has_tick() override { return true; }
    void s_tick(unsigned long now_s) override;
    bool has_loop() override { return true; }   // not s_tick, yield panics there
    uint16_t loop_interval_ms() override { return 1000; }   // never 0
    void loop(unsigned long now) override;
    void begin() override;
    const EGPrefGroup* prefs_group() override;
    void on_prefs_loaded() override;
    void registerRoutes(EGHttpServer& http) override;
    const EGMenuEntry* menuEntries() override;
    bool deleteOldest(uint32_t cutoffPacked);   // DOS (date<<16)|time
    void pauseWriter();
    // SdFat not reentrant
    struct Hold {
      Hold();
      ~Hold();
    };
    void reinit();
    bool ready() const { return sdenabled; }
  protected:
    File32 myDataFile;
  private:
    void disableFull();
    time_t lastWrittenMinute = 0;
    uint32_t openFileDay = 0;  // YYYYMMDD, 0 = closed
    uint8_t _sync_min = 1;
    uint8_t _unsynced_writes = 0;
    bool sdenabled = false;
    bool _freecheck_pending = true;
    enum : uint8_t { CLEAN_NONE = 0, CLEAN_FREECHECK, CLEAN_FULL };
    uint8_t _cleanup = CLEAN_NONE;
    bool _busy = false;
    bool _write_due = false;
    void writeMinute();
    bool openDayFile(const char* dirStr, const char* timeStr, uint32_t day, bool& failed);
    void scheduleCleanup(uint8_t what);
};

extern SDCard sdcard;

#endif
#endif
