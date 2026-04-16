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

  if (!myDataFile.open(".test.txt", O_WRITE | O_CREAT | O_TRUNC)) {
    Log::console(PSTR("SDCard: Test file failed."));
    delete sd;
    sd = nullptr;
    return;
  }
  if(sd->exists(".test.txt"))
  {
    sd->remove(".test.txt");
  }

  myDataFile.close();

  if (!sd->volumeBegin()) {
    Log::console(PSTR("SDCard: volumeBegin failed. Is the card formatted?"));
    delete sd;
    sd = nullptr;
    return;
  }

  Log::console(PSTR("SDCard: Calculating free space, please wait"));
  uint64_t freespace = sd->freeClusterCount() * sd->sectorsPerCluster() * 0.000512;
  if (freespace <= 8) {
    deleteOldest();
  }
  Log::console(PSTR("SDCard: OK! %lld MB free."), freespace);
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

  struct tm *timeinfo = gmtime (&currentTime);
  if (timeinfo->tm_sec != 0) {
    return;
  }
  // Guard against multiple fires within the same real minute
  time_t thisMinute = currentTime / 60;
  if (thisMinute == lastWrittenMinute) {
    return;
  }
  lastWrittenMinute = thisMinute;

  if (!sd->chdir()) {
    Log::console(PSTR("SDCard: Cannot chdir to root"));
    return;
  }

  bool forceCleanup = false;
  char timeStr[23];
  char dirStr[20];
  char dateTime[20];

  snprintf_P (timeStr, sizeof (timeStr), PSTR("%04d%02d%02d.csv"), 1900+timeinfo->tm_year, timeinfo->tm_mon+1, timeinfo->tm_mday);
  snprintf_P (dirStr, sizeof (dirStr), PSTR("%04d%02d"), 1900+timeinfo->tm_year, timeinfo->tm_mon+1);
  snprintf_P (dateTime, sizeof (dateTime), PSTR("%04d-%02d-%02d %02d:%02d:%02d"),
    1900+timeinfo->tm_year, timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec
  );

  if (!sd->exists(dirStr)) {
    sd->mkdir(dirStr);
  }
  sd->chdir(dirStr);

  bool fileExists = false;
  if(sd->exists(timeStr)) {
    fileExists = true;
  }

  myDataFile.open(timeStr, FILE_WRITE);
  if (myDataFile)
  {
    if (!fileExists) {
      myDataFile.print(F("DateTime,CPM,μSv/h,CPM5,CPM15"));
#ifdef SDCARD_EXTENDEDLOG
      myDataFile.print(F(",FreeMem"));
#endif
      myDataFile.println();
    }
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
    myDataFile.close();
  } else {
    Log::console(PSTR("SDCard: Unable to create file."));
    forceCleanup = true;
  }

  if ((millis() - lastClean >= 86400*1000) || (forceCleanup)) {
    lastClean = millis();
    uint8_t maxDeletes = 10;
    uint64_t freespace = sd->freeClusterCount() * sd->sectorsPerCluster() * 0.000512;
    while (freespace <= 8 && maxDeletes-- > 0) {
      deleteOldest();
      freespace = sd->freeClusterCount() * sd->sectorsPerCluster() * 0.000512;
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