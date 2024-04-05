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
static EspSoftwareSerial::UART geigerPort;

/*

MightyOhm CPS, 1, CPM, 60, uSv/hr, 1.23, INST/FAST/SLOW\n
GC10 60\n

*/
#include "../GeigerInput.h"

#ifndef GEIGER_BAUDRATE
#define GEIGER_BAUDRATE 9600
#endif

#ifndef GEIGER_TXPIN
#define GEIGER_TXPIN -1
#endif

#ifndef GEIGER_SERIAL_TYPE
#define GEIGER_SERIAL_TYPE GEIGER_SERIAL_CPM
#endif


class GeigerSerial : public GeigerInput
{
  public:
    GeigerSerial();
    void begin();
    void loop();
    void secondTicker();
  private:
    char _serial_buffer[64];
    uint8_t _serial_idx = 0;
    void handleSerial(char* input);
    float partial_clicks = 0;
    int serial_value = 0;
    unsigned long last_serial;
    int avg_diff;
};
#endif