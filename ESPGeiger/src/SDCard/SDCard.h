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
#include "App/AppConfig.h"
#include "Counter/Counter.h"
#include "Corium/Module/Module.h"
#include "Corium/Module/Registry.h"
#include "App/Topics.h"
#include "Corium/Prefs/Prefs.h"

#define SD_EDGE_MS 50   // past :00, the clock has rolled

extern SdFat32* sd;

// A csv row a minute in /YYYYMM/YYYYMMDD.csv, oldest year-old files go when full
class SDCard : public corium::Module {
  public:
    SDCard();
    const char* name() override { return PSTR("sdcard"); }
    uint8_t displayOrder() override { return 20; }
    void loop(unsigned long now) override;
    void begin() override;
    const corium::PrefGroup* prefsGroup() override;
    void onPrefsLoaded() override;
    void registerRoutes(EGHttpServer& http) override;
    const corium::MenuEntry* menuEntries() override;
    bool deleteOldest(uint32_t cutoff);   // DOS (date<<16)|time
    void pauseWriter();
    // SdFat not reentrant, closes the day file first
    struct Hold {
      Hold();
    };
    void reinit();
    bool ready() const { return _enabled; }   // writing; a stopped card still reads
    bool mounted() const { return sd != nullptr; }
    int32_t freeMB() const;   // -1 = counting or a count failed
    bool counting() const { return _counting; }
    // a card there that won't mount or write, once till it mounts
    static constexpr uint8_t TOPIC_ERROR = Topic::SD_ERROR;
    bool faulted() const { return _fault; }
  protected:
    File32 _file;
  private:
    void disableFull();
    void fault();
    void fail_step(const char* step, const char* dir, const char* file);
    bool _fault = false;
    uint8_t _fails = 0;        // failed minutes in a row
    uint32_t _fail_min = 0;    // the last one counted
    time_t _last_minute = 0;
    uint32_t _open_day = 0;  // YYYYMMDD, 0 = closed
    uint8_t _sync_min = 1;
    uint8_t _unsynced_writes = 0;
    bool _enabled = false;
    enum : uint8_t { CLEAN_NONE = 0, CLEAN_FREECHECK, CLEAN_FULL };
    uint8_t _cleanup = CLEAN_NONE;
    uint8_t _counting = CLEAN_NONE;   // the cleanup it's counting for
    int32_t _free = -1;               // clusters
    bool _undated = false;   // its header written this boot
    uint32_t _file_size = 0;          // day file's, at the last row
    uint32_t _fat_sec = 0;            // next FAT sector
    void used_clusters(uint32_t n);
    void minuteEdge();
    void writeMinute();
    void writeUndated();
    bool wrote(const char* dir, const char* name);
    void afterRow(bool clean);
    void closeDay(bool sync);
    bool openDayFile(const char* dir, const char* name, uint32_t day, bool& failed);
    void scheduleCleanup(uint8_t what);
    void startCount(uint8_t what);
    void countFat();
    void cleanup(uint8_t what);
};

extern SDCard sdcard;

#endif
#endif
