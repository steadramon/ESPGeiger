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
#include <Arduino.h>

// Before GeigerInput.h, which seeds _tx_pin from it. Same order as
// TestSerial.h and TestPulse.h.
#ifndef GEIGER_TXPIN
#define GEIGER_TXPIN -1
#endif

#include "SerialPort.h"

// TEMPORARY - GC10Next hardware-watchdog hunt. Defined in Serial.cpp, read by
// WebPortal's /info handler. Remove both together.
namespace GeigerSerialDiag {
  extern uint32_t      ovf;        // decoder outrun
  extern uint32_t      buf_ovf;    // byte-buffer overruns
  extern uint32_t      isr_drops;  // declined or muted by the admission budget
  extern uint32_t      framing;    // EGTinySerial only
  extern uint32_t      coalesced;  // EGTinySerial only
  extern uint32_t      breaks;     // EGTinySerial only
  extern uint32_t      rx_bytes;   // EGTinySerial only
  extern uint32_t      isr_calls;  // EG_TINYSERIAL_BENCH only
  extern uint32_t      isr_max;    // EG_TINYSERIAL_BENCH only
  extern uint32_t      lines_ok;   // lines that parsed
  extern uint8_t       bad_peak;
  extern uint8_t       max_bytes;
  extern unsigned long last_drain;
  extern uint32_t      drains;
}
#include "../GeigerInput.h"
#include "../../Util/FastMillis.h"

class GeigerSerial : public GeigerInput
{
  public:
    GeigerSerial();
    void begin();
    void loop();
    void secondTicker();
    // The handler has no business burning cycles during a flash write, and
    // anything received mid update is stale.
    void stopForOTA() override;
    void restartAfterOTA() override;
    bool isHealthy() const override {
      if (_last_drain != 0 && (fast_millis() - _last_drain) < 60000) return false;
      return true;
    }
    void stopForOTA() override;
    void restartAfterOTA() override;
  private:
    void pullSerial();
    char _serial_buffer[64];
    uint8_t _serial_idx = 0;
    // fast_millis of the last byte taken off the port. Ages out a line that
    // never terminates; see loop().
    unsigned long _line_ms = 0;
    void handleSerial(char* input);
    float partial_clicks = 0;
    int serial_value = 0;
    unsigned long last_serial = 0;
    uint16_t _loop_c = 0;
    uint16_t _poll_skip = 5;  // empty-buffer poll throttle, scaled to baud in begin()
    uint8_t _serial_type = GEIGER_SERIALTYPE;
    uint8_t _bad_streak = 0;
    unsigned long _last_drain = 0;
    unsigned long _last_drain_log = 0;
    void drainPort();
    bool _use_cps = false;
};
#endif
