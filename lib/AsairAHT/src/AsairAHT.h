/*
  AsairAHT.h - Lean Asair AHT10 / AHT20 driver.

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
#ifndef ASAIRAHT_H
#define ASAIRAHT_H

#include <Arduino.h>
#include <Wire.h>

namespace AsairAHT {

class Sensor {
  public:
    bool begin(TwoWire& wire);

    // wait >= 80 ms between
    bool trigger();
    bool readResult(float& t_c, float& h_pct, float& p_hpa);

    bool read(float& t_c, float& h_pct, float& p_hpa);

    bool    present() const { return _present; }
    uint8_t i2cAddr() const { return ADDR; }
    const __FlashStringHelper* chipName() const { return F("AHTxx"); }

  private:
    static constexpr uint8_t ADDR = 0x38;

    bool status(uint8_t& s);
    bool sendInit(const uint8_t* seq);
    bool trusted(const uint8_t* buf);

    // AHT10/15 send no CRC
    enum Kind : uint8_t { KIND_UNKNOWN, KIND_CRC, KIND_PLAIN };

    TwoWire* _wire = nullptr;
    bool     _present = false;
    uint8_t  _kind = KIND_UNKNOWN;
    uint8_t  _misses = 0;
};

}

#endif
