/*
  Tm1637Driver.h - Minimal bit-banged TM1637 driver (4-6 digit 7-seg modules).

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
#ifndef EG_TM1637DRIVER_H
#define EG_TM1637DRIVER_H
#include <Arduino.h>

// LSB first, DIO let go for the ack, no ack = not there
class Tm1637Driver {
  public:
    static constexpr uint8_t MAX_DIGITS = 6;
    static constexpr uint8_t SLOWEST_US = 100;

    void begin(int8_t clk, int8_t dio);
    bool ready() const { return _ready; }
    uint8_t half_clock_us() const { return _us; }

    // leftmost first, bit 7 = dot or colon, only the changed digits sent
    bool show(const uint8_t* segs, uint8_t n);
    bool light(uint8_t level, bool on);   // 0..7

  private:
    void drive(int8_t pin, bool high);
    void release(int8_t pin);
    void start();
    void stop();
    bool put(uint8_t b);
    bool command(uint8_t c);

    int8_t  _clk = -1, _dio = -1;
    bool    _ready = false;
    uint8_t _us = SLOWEST_US;   // half a clock
    uint8_t _ctrl = 0x88;       // on, dimmest
    uint8_t _last[MAX_DIGITS];
    uint8_t _lastN = 0;
};

#endif
