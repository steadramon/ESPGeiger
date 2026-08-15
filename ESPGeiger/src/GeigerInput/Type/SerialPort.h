/*
  GeigerInput/Type/SerialPort.h - the serial port the geiger inputs talk to.

  Copyright (C) 2026 @steadramon

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

// One shape over two backends so Serial.cpp and TestSerial.cpp do not care
// which is underneath. Build with -DEG_USE_TINYSERIAL for EGTinySerial,
// without it for EspSoftwareSerial: the core's bundled copy on ESP8266, the
// plerup registry one on ESP32. Capacity must be a power of two.

#ifndef GEIGERSERIALPORT_H
#define GEIGERSERIALPORT_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

// Monotonic since begin(). EspSoftwareSerial's flags are read-and-clear, so
// that backend accumulates them here rather than leaving callers to: a
// second reader would otherwise take an event off the first.
struct GeigerPortStats {
  uint32_t isr_ovf   = 0;   // decoder outrun. Structurally impossible on EGTinySerial, which has no ISR buffer
  uint32_t buf_ovf   = 0;   // byte buffer overran: this poll is too slow
  uint32_t declined  = 0;   // admission budget: frames declined, or windows the pin was masked
  uint32_t framing   = 0;   // EGTinySerial only
  uint32_t coalesced = 0;   // EGTinySerial only
  uint32_t breaks    = 0;   // EGTinySerial only
  uint32_t bytes     = 0;   // EGTinySerial only: delivered to the ring
  uint32_t isr_calls = 0;   // EG_TINYSERIAL_BENCH only
  uint32_t isr_max   = 0;   // EG_TINYSERIAL_BENCH only: worst handler, cycles
  uint32_t stalls    = 0;   // EGTinySerial only: handler gave up on its own deadline
};

#ifdef EG_USE_TINYSERIAL

#include <EGTinySerial.h>

template <uint16_t Cap>
class GeigerPort {
  public:
    bool   begin(uint32_t baud, int rx, int tx) { return _port.begin(baud, (int8_t)rx, (int8_t)tx); }
    int    available()                          { return _port.available(); }
    int    read()                               { return _port.read(); }
    size_t write(const uint8_t* b, size_t n)    { return _port.write(b, n); }
    void   pollStats()                          {}   // already monotonic
#ifdef ESP32
    void   tick()                               { _port.tick(); }
#else
    // Nothing changes the ESP8266 CPU frequency at runtime, so this would be
    // a cold 1 Hz flash region earning nothing.
    void   tick()                               {}
#endif
    void   pause()                              { _port.pause(); }
    void   resume()                             { _port.resume(); }

    GeigerPortStats stats() const {
      const EGTinySerial::Stats& s = _port.stats();
      GeigerPortStats o;
      o.buf_ovf   = s.dropped;
      o.declined  = s.muted;
      o.framing   = s.framing;
      o.coalesced = s.coalesced;
      o.breaks    = s.breaks;
      o.bytes     = s.bytes;
      o.stalls    = s.stalls;
#ifdef EG_TINYSERIAL_BENCH
      o.isr_calls = s.isrCalls;
      o.isr_max   = s.isrMaxCycles;
#endif
      return o;
    }

  private:
    EGTinySerial::Port<Cap> _port;
};

#else

#include <SoftwareSerial.h>
namespace EGSoftSerial = EspSoftwareSerial;

template <uint16_t Cap>
class GeigerPort {
  public:
    bool begin(uint32_t baud, int rx, int tx) {
      _port.begin(baud, SWSERIAL_8N1, (int8_t)rx, (int8_t)tx, false, Cap);
      return true;   // no failure path to report
    }
    int    available()                       { return _port.available(); }
    int    read()                            { return _port.read(); }
    size_t write(const uint8_t* b, size_t n) { return _port.write(b, n); }

    void pollStats() {
      // Upstream merges the two opposite overflow causes into one flag and
      // has no admission budget.
      if (_port.overflow()) _s.isr_ovf++;
    }
    GeigerPortStats stats() const { return _s; }
    // Times in wall clock, so a CPU frequency change does not affect it.
    void tick() {}
    void pause()  { _port.enableRx(false); }
    void resume() { _port.enableRx(true); }

  private:
    EGSoftSerial::UART _port;
    GeigerPortStats    _s;
};

#endif // EG_USE_TINYSERIAL

#endif
