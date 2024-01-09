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
#include "../Status.h"
#include "../Counter/Counter.h"

#ifndef GEIGER_SDCARD_CS
#define GEIGER_SDCARD_CS 16
#endif

extern Status status;
extern Counter gcounter;
static SdFat32 sd;

class SDCard {
  public:
  static SDCard &getInstance()
  {
    static SDCard instance;
    return instance;
  }
    void loop();
    void begin();
    void deleteOldest();
  protected:
    File32 myDataFile;
  private:
    SDCard();
    unsigned long lastLog = 0;
    unsigned long lastClean = 0;
    unsigned long logInterval = 60 * 1000;
    bool sdenabled = false;
};
#endif
#endif