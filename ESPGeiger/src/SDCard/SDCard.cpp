/*
  SDCard.cpp - functions to handle SDCard
  
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
#ifdef GEIGER_SDCARD
#include <Arduino.h>
#include "SDCard.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"

SdFat32* sd = nullptr;

SDCard sdcard;
EG_REGISTER_MODULE(sdcard)

static const EGPref SDCARD_PREF_ITEMS[] = {
  {"sync_min", "Sync Interval (min)", "Minutes between syncs. 1=safest, higher=less SD wear", "1", nullptr, 1, 5, 0, EGP_UINT, 0},
};

static const EGPrefGroup SDCARD_PREF_GROUP = {
  "sdcard", "SD Card", 1,
  SDCARD_PREF_ITEMS,
  sizeof(SDCARD_PREF_ITEMS) / sizeof(SDCARD_PREF_ITEMS[0]),
};

const EGPrefGroup* SDCard::prefs_group() { return &SDCARD_PREF_GROUP; }

void SDCard::on_prefs_loaded() {
  uint32_t v = EGPrefs::getUInt("sdcard", "sync_min");
  if (v < 1) v = 1;
  if (v > 5) v = 5;
  _sync_min = (uint8_t)v;
}

static void dateTimeCB(uint16_t *dosYear, uint16_t *dosTime) {
    time_t now;
    now = time(nullptr);
    struct tm *tiempo = localtime(&now);
    *dosYear = ((tiempo->tm_year - 80) << 9) | ((tiempo->tm_mon + 1) << 5) | tiempo->tm_mday;
    *dosTime = (tiempo->tm_hour << 11) | (tiempo->tm_min << 5) | tiempo->tm_sec;
}

SDCard::SDCard() {
}

void SDCard::begin()
{
  Log::console(PSTR("SDCard: Init ..."));
  sd = new SdFat32();
  if (!sd->begin(GEIGER_SDCARD_CS))
  {
    Log::console(PSTR("SDCard: Init failed ... Please check wiring or insert a card."));
    delete sd;
    sd = nullptr;
    return;
  }
  FsDateTime::setCallback(dateTimeCB);

  if (!myDataFile.open("TEST.TMP", O_WRITE | O_CREAT | O_TRUNC)) {
    Log::console(PSTR("SDCard: Test file failed."));
    delete sd;
    sd = nullptr;
    return;
  }
  if(sd->exists("TEST.TMP"))
  {
    sd->remove("TEST.TMP");
  }

  myDataFile.close();

  if (!sd->volumeBegin()) {
    Log::console(PSTR("SDCard: volumeBegin failed. Is the card formatted?"));
    delete sd;
    sd = nullptr;
    return;
  }

  Log::console(PSTR("SDCard: Calculating free space, please wait"));
  // sectors = freeClusterCount * sectorsPerCluster; MB = sectors / 2048 (1 sector = 512 B).
  uint32_t freespace = (uint32_t)(((uint64_t)sd->freeClusterCount() * sd->sectorsPerCluster()) >> 11);
  if (freespace <= 8) {
    deleteOldest();
  }
  Log::console(PSTR("SDCard: OK! %lu MB free."), (unsigned long)freespace);
  sdenabled = true;
}

void SDCard::s_tick(unsigned long stick_now)
{
  if (!sdenabled) {
    return;
  }

  time_t currentTime = time (NULL);
  if (currentTime == 0) {
    return;
  }

  // Fire once per new minute. Check minute bucket before gmtime - gmtime
  // is ~100us on ESP8266 so skipping it on 59/60 ticks saves real time.
  time_t thisMinute = currentTime / 60;
  if (thisMinute == lastWrittenMinute) {
    return;
  }
  if (lastWrittenMinute == 0) {
    // First valid tick after boot - don't write immediately, just prime.
    lastWrittenMinute = thisMinute;
    return;
  }
  lastWrittenMinute = thisMinute;

  struct tm *timeinfo = gmtime (&currentTime);

  bool forceCleanup = false;
  char timeStr[23];
  char dirStr[20];
  char dateTime[20];

  snprintf_P (timeStr, sizeof (timeStr), PSTR("%04d%02d%02d.csv"), 1900+timeinfo->tm_year, timeinfo->tm_mon+1, timeinfo->tm_mday);
  snprintf_P (dirStr, sizeof (dirStr), PSTR("%04d%02d"), 1900+timeinfo->tm_year, timeinfo->tm_mon+1);
  snprintf_P (dateTime, sizeof (dateTime), PSTR("%04d-%02d-%02d %02d:%02d:%02d"),
    1900+timeinfo->tm_year, timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec
  );

  uint32_t currentDay = (1900+timeinfo->tm_year) * 10000u
                      + (timeinfo->tm_mon+1) * 100u
                      + timeinfo->tm_mday;

  // Open file only on first write or day rollover - saves the open() cost
  // (~10-30ms) on every other minute. sync() below still flushes per-minute
  // so durability is unchanged.
  if (currentDay != openFileDay || !myDataFile.isOpen()) {
    if (myDataFile.isOpen()) {
      myDataFile.close();
    }
    openFileDay = 0;
    _unsynced_writes = 0;

    if (!sd->chdir()) {
      Log::console(PSTR("SDCard: Cannot chdir to root"));
      return;
    }
    if (!sd->exists(dirStr)) {
      sd->mkdir(dirStr);
    }
    sd->chdir(dirStr);

    bool fileExists = sd->exists(timeStr);
    if (!myDataFile.open(timeStr, FILE_WRITE)) {
      Log::console(PSTR("SDCard: Unable to create file."));
      forceCleanup = true;
    } else {
      if (!fileExists) {
        myDataFile.print(F("DateTime,CPM,uSv/h,CPM5,CPM15"));
#ifdef SDCARD_EXTENDEDLOG
        myDataFile.print(F(",FreeMem"));
#endif
        myDataFile.println();
      }
      openFileDay = currentDay;
    }
  }

  if (myDataFile.isOpen()) {
#ifdef SDCARD_LOG_UNIXTIME
    myDataFile.print(time(NULL));
#else
    myDataFile.print(dateTime);
#endif
    myDataFile.print(F(","));
    myDataFile.print(gcounter.get_cpmf(), 2);
    myDataFile.print(F(","));
    myDataFile.print(gcounter.get_usv(), 4);
    myDataFile.print(F(","));
    myDataFile.print(gcounter.get_cpm5f(), 2);
    myDataFile.print(F(","));
    myDataFile.print(gcounter.get_cpm15f(), 2);
#ifdef SDCARD_EXTENDEDLOG
    myDataFile.print(F(","));
    myDataFile.print(ESP.getFreeHeap());
#endif
    myDataFile.println();

    // Sync every _sync_min writes. Between syncs, data is buffered in RAM
    // only - power loss risks losing up to (_sync_min - 1) rows.
    _unsynced_writes++;
    if (_unsynced_writes >= _sync_min) {
      if (!myDataFile.sync()) {
        myDataFile.close();
        openFileDay = 0;
        forceCleanup = true;
      }
      _unsynced_writes = 0;
    }
  }

  static uint8_t clean_sec = 0;
  static uint16_t clean_min = 0;
  bool clean_due = false;
  if (++clean_sec >= 60) { clean_sec = 0; if (++clean_min >= 1440) { clean_min = 0; clean_due = true; } }
  if (clean_due || forceCleanup) {
    // Close before cleanup so deleteOldest never races our handle.
    if (myDataFile.isOpen()) {
      myDataFile.close();
      openFileDay = 0;
      _unsynced_writes = 0;
    }
    uint8_t maxDeletes = 10;
    uint32_t freespace = (uint32_t)(((uint64_t)sd->freeClusterCount() * sd->sectorsPerCluster()) >> 11);
    while (freespace <= 8 && maxDeletes-- > 0) {
      deleteOldest();
      freespace = (uint32_t)(((uint64_t)sd->freeClusterCount() * sd->sectorsPerCluster()) >> 11);
    }
  }
}

void SDCard::deleteOldest(){
  File32 rootDir;
  File32 subDir;
  File32 file;
  char oldestDir[13] = "";
  char oldestFile[13] = "";
  uint32_t oldestModified = 0xFFFFFFFFul;

  if (!sd->chdir()) {
    return;
  }
  if (!rootDir.open("/")) {
    return;
  }

  // Walk each YYYYMM/ subdirectory to find the oldest .csv
  while (subDir.openNext(&rootDir, O_RDONLY)) {
    if (subDir.isSubDir() && !subDir.isHidden()) {
      char dirName[13];
      subDir.getName(dirName, sizeof(dirName));

      while (file.openNext(&subDir, O_RDONLY)) {
        if (!file.isSubDir() && !file.isHidden()) {
          char f_name[13];
          file.getName(f_name, sizeof(f_name));
          size_t nameLen = strlen(f_name);
          if (nameLen >= 4 && strcmp(f_name + nameLen - 4, ".csv") == 0) {
            uint16_t date_AGPS, time_AGPS;
            file.getModifyDateTime(&date_AGPS, &time_AGPS);
            uint32_t lastModified = (uint32_t(date_AGPS) << 16) | time_AGPS;

            if (lastModified < oldestModified) {
              oldestModified = lastModified;
              strncpy(oldestDir, dirName, sizeof(oldestDir) - 1);
              oldestDir[sizeof(oldestDir) - 1] = '\0';
              strncpy(oldestFile, f_name, sizeof(oldestFile) - 1);
              oldestFile[sizeof(oldestFile) - 1] = '\0';
            }
          }
        }
        file.close();
      }
    }
    subDir.close();
  }
  rootDir.close();

  if (oldestFile[0] == '\0') {
    return;
  }

  if (sd->chdir(oldestDir) && sd->exists(oldestFile)) {
    Log::console(PSTR("SDCard: Deleting old file %s/%s"), oldestDir, oldestFile);
    sd->remove(oldestFile);
  }
  sd->chdir();
}
#endif