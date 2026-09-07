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

// EG_SERIAL_NMI receiver or EspSoftwareSerial. Cap a power of two

#ifndef GEIGERSERIALPORT_H
#define GEIGERSERIALPORT_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

#if defined(EG_SERIAL_NMI)

#if !defined(ESP8266) || !defined(EG_TIMER1)
#error "EG_SERIAL_NMI: ESP8266 with EG_TIMER1 only"
#endif
#ifdef ESPG_HV
#error "EG_SERIAL_NMI and ESPG_HV both need EGTimer1"
#endif
#include <EGTimer1.h>

// rx only, handler owns the ring
template <uint16_t Cap>
class GeigerPort {
  public:
    bool   begin(uint32_t baud, int rx, int tx) { (void)tx; return EGTimer1::rx_begin((int8_t)rx, baud); }
    int    available()                          { return EGTimer1::rx_available(); }
    int    read()                               { return EGTimer1::rx_read(); }
    size_t write(const uint8_t* b, size_t n)    { (void)b; (void)n; return 0; }
    void   flush()                              { while (available()) read(); }
    void   pause()                              { EGTimer1::rx_pause(); }
    void   resume()                             { EGTimer1::rx_resume(); }
};

#else

#include <SoftwareSerial.h>

template <uint16_t Cap>
class GeigerPort {
  public:
    bool begin(uint32_t baud, int rx, int tx) {
      _port.begin(baud, SWSERIAL_8N1, (int8_t)rx, (int8_t)tx, false, Cap);
      return true;
    }
    int    available()                       { return _port.available(); }
    int    read()                            { return _port.read(); }
    size_t write(const uint8_t* b, size_t n) { return _port.write(b, n); }
    void   flush()                           { while (available()) read(); }
    void pause()  { _port.enableRx(false); }
    void resume() { _port.enableRx(true); }

  private:
    EspSoftwareSerial::UART _port;
};

#endif // EG_SERIAL_NMI

#endif
