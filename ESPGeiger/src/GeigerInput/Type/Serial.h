/*
  GeigerInput/Type/Serial.h - Class for Serial type counter

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
#ifndef GEIGERINPUTSRL_H
#define GEIGERINPUTSRL_H
#include <stdint.h>
#include "Corium/Platform/Flash.h"

// before GeigerInput.h
#ifndef GEIGER_TXPIN
#define GEIGER_TXPIN -1
#endif

#include "SerialPort.h"

#include "GeigerInput/GeigerInput.h"
#include "Corium/System/FastMillis.h"

class GeigerSerial final : public GeigerInput
{
  public:
    GeigerSerial();
    void begin() override;
    void loop() override;
    void secondTicker() override;
    void stopForOTA() override;
    void restartAfterOTA() override;
    bool isHealthy() const override { return _ok; }
  private:
    void pullSerial();
    char _serial_buffer[64];
    uint8_t _serial_idx = 0;
    bool _discard = false;   // rest of a bad line, up to its \n
    void drop_line();
    unsigned long _line_ms = 0;   // fast_millis()
    void handleSerial(char* input);
    float partial_clicks = 0;
    int serial_value = 0;
    unsigned long last_serial = 0;
    uint8_t _idle_ms = 4;  // scaled to baud in begin()
    uint8_t _serial_type = GEIGER_SERIALTYPE;
    uint8_t _bad_streak = 0;
    Stamp _line;   // good line
    Stamp _drain;
    bool _ok = false;
    unsigned long _last_drain_log = 0;
    void drainPort();
    bool _use_cps = false;
};
#endif
