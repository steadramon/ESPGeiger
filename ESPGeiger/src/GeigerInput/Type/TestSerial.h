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
#include <stdint.h>
#include "Corium/Platform/Flash.h"
#include "SerialPort.h"
#include "GeigerInput/GeigerInputTest.h"

// a line a second on TX, RX read back to the log
class GeigerTestSerial final : public GeigerInputTest
{
  public:
    GeigerTestSerial();
    void begin() override;
    void loop() override;
    void secondTicker() override;
  private:
    void pullSerial();
    void handleSerial(char* input);
    void send_line();

    char _buf[64];
    uint8_t _pos = 0;
    bool _send = false;   // set by the tick, loop() writes the line
    uint8_t _idle_ms = 1;  // scaled to baud in begin()
    uint8_t _serial_type = GEIGER_SERIALTYPE;
};
#endif
