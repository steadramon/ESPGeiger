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
  if (!sd.begin(16))
  {
    Log::console(PSTR("SDCard: Init failed ... Please check wiring or insert a card."));
    return;
  }
  FsDateTime::setCallback(dateTimeCB);

  if (!myDataFile.open(".test.txt", O_WRITE | O_CREAT | O_TRUNC)) {
    Log::console(PSTR("SDCard: Test file failed."));
    return;
  }
  if(sd.exists(".test.txt"))
  {
    sd.remove(".test.txt");
  }

  myDataFile.close();

  if (!sd.volumeBegin()) {
    Log::console(PSTR("SDCard: volumeBegin failed. Is the card formatted?"));
    return;
  }

  Log::console(PSTR("SDCard: Calculating free space, please wait"));
  uint64_t freespace = sd.freeClusterCount() * sd.sectorsPerCluster() * 0.000512;
  if (freespace <= 8) {
    deleteOldest();
  }
  Log::console(PSTR("SDCard: OK! %lld MB free."), freespace);
  sdenabled = true;
}

void SDCard::loop()
{
  if (!sdenabled) {
    return;
  }
  if (status.ntp_synced == false) {
    return;
  }
  unsigned long millisnow = millis();
  bool forceCleanup = false;
  if (millisnow - lastLog >= logInterval) {
    lastLog = millisnow;
    char timeStr[23];

    time_t currentTime = time (NULL);
    if (currentTime > 0) {
        struct tm *timeinfo = localtime (&currentTime);
        snprintf_P (timeStr, sizeof (timeStr), "%04d%02d%02d.csv", 1900+timeinfo->tm_year, timeinfo->tm_mon+1, timeinfo->tm_mday);
    } else {
        snprintf_P (timeStr, sizeof (timeStr), "log.csv");
    }
    bool fileExists = false;
    if(sd.exists(timeStr)) {
      fileExists = true;
    }

    myDataFile.open(timeStr, FILE_WRITE);
    if (myDataFile)
    {
      if (!fileExists) {
        myDataFile.print(F("Unixtime,CPM,CPM5,CPM15"));
#ifdef SDCARD_EXTENDEDLOG
        myDataFile.print(F(",FreeMem"));
#endif
        myDataFile.println();
      }
      myDataFile.print(time(NULL));
      myDataFile.print(F(","));
      myDataFile.print(status.geigerTicks.get()*60.0, 2);
      myDataFile.print(F(","));
      myDataFile.print(status.geigerTicks5.get()*60.0, 2);
      myDataFile.print(F(","));
      myDataFile.print(status.geigerTicks15.get()*60.0, 2);
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
  }
  if ((millis() - lastClean >= 86400*1000) || (forceCleanup)) {
    lastClean = millis();
    uint64_t freespace = sd.freeClusterCount() * sd.sectorsPerCluster() * 0.000512;
    if (freespace <= 8) {
        deleteOldest();
    }
  }
}

void SDCard::deleteOldest(){ 
  File32 dirFile;
  File32 file;
  char oldestFile[13];

  uint32_t oldestModified = 0xFFFFFFFFul;

  if (!dirFile.open("/")) {
    sdenabled=false;
    return;
  }

   while (file.openNext(&dirFile, O_RDONLY)) {
    // Skip directories and hidden files.
    if (!file.isSubDir() && !file.isHidden()) {
      char f_name[13];
      file.getName(f_name, 13);
      char fileext[5];
      memcpy( fileext, &f_name[8], 4 );
      fileext[4] = '\0';
      if (strcmp(fileext, ".csv") == 0) {
        uint16_t date_AGPS, time_AGPS;
        file.getModifyDateTime(&date_AGPS, &time_AGPS);
        uint32_t lastModified = (uint32_t (date_AGPS) << 16 | time_AGPS);

        if (lastModified < oldestModified ) {
            oldestModified = lastModified;
            memcpy( oldestFile, &f_name, 13 );
        }
      }
    }
    file.close();
  }
  dirFile.close();
  if(sd.exists(oldestFile))
  {
    Log::console(PSTR("SDCard: Deleting old file %s"), oldestFile);
    sd.remove(oldestFile);
  }
}
#endif