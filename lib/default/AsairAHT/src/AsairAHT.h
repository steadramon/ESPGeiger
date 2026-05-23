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

    bool read(float& t_c, float& h_pct, float& p_hpa);

    bool    present() const { return _present; }
    uint8_t i2cAddr() const { return 0x38; }
    const __FlashStringHelper* chipName() const { return F("AHTxx"); }

  private:
    bool status(uint8_t& s);
    bool sendInit();
    bool trigger();

    TwoWire* _wire = nullptr;
    bool     _present = false;
};

}

#endif
