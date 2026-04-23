/*
  GeigerInput/Type/TestSerial.h - Class for Test Serial type counter

  Copyright (C) 2024 @steadramon

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
#ifndef GEIGERTESTSRL_H
#define GEIGERTESTSRL_H
#include <Arduino.h>
#include <SoftwareSerial.h>
#include "../../Util/EGSmoothed.h"

#ifndef GEIGER_TXPIN
#define GEIGER_TXPIN 12
#endif

#include "../GeigerInputTest.h"

class GeigerTestSerial : public GeigerInputTest
{
  public:
    GeigerTestSerial();
    void begin();
    void loop();
    void secondTicker();
  private:
    void pullSerial();
    void handleSerial(char* input);

    char _serial_buffer[64];
    uint8_t _serial_idx = 0;
    uint8_t _loop_c = 0;
    uint8_t _serial_type = GEIGER_SERIALTYPE;
};
#endif
