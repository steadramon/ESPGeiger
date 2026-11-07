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

// Counter on a uart, sends cpm (and maybe cps) lines
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
    inline __attribute__((always_inline)) void clear_line() {
      _serial_idx = 0;
      _serial_buffer[0] = '\0';
    }
    void bad_line();
    void drop_line();
    unsigned long _rx_ms = 0;   // fastMillis()
    void handleSerial(char* input);
    float _carry = 0;
    int _cpm = 0;
    unsigned long _good_ms = 0;
    uint8_t _idle_ms = 4;  // scaled to baud in begin()
    uint8_t _serial_type = GEIGER_SERIALTYPE;
    uint8_t _bad_streak = 0;
    corium::Stamp _line;   // good line
    corium::Stamp _drain;
    corium::Stamp _cps_seen;
    bool _ok = false;
    unsigned long _last_drain_log = 0;
    void drainPort();
    bool _use_cps = false;
};
#endif
