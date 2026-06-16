/*
  BoschTHP.h - Lean BME280 / BMP280 driver.

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
#ifndef BOSCHTHP_H
#define BOSCHTHP_H

#include <Arduino.h>
#include <Wire.h>

namespace BoschTHP {

enum ChipId : uint8_t {
  CHIP_NONE   = 0x00,
  CHIP_BMP280 = 0x58,
  CHIP_BME280 = 0x60,
};

class Sensor {
  public:
    bool begin(TwoWire& wire, uint8_t addr = 0);

    bool read(float& t_c, float& h_pct, float& p_hpa);

    bool    present() const { return _chip != CHIP_NONE; }
    uint8_t chipId() const  { return _chip; }
    uint8_t i2cAddr() const { return _addr; }
    const __FlashStringHelper* chipName() const;

  private:
    bool probe(uint8_t addr);
    bool readCal();
    bool configure();
    bool read8 (uint8_t reg, uint8_t& out);
    bool readN (uint8_t reg, uint8_t* out, uint8_t len);
    bool write8(uint8_t reg, uint8_t val);

    TwoWire* _wire = nullptr;
    uint8_t  _addr = 0;
    uint8_t  _chip = CHIP_NONE;
    uint16_t T1 = 0; int16_t T2 = 0, T3 = 0;
    uint16_t P1 = 0; int16_t P2 = 0, P3 = 0, P4 = 0, P5 = 0, P6 = 0, P7 = 0, P8 = 0, P9 = 0;
    uint8_t  H1 = 0, H3 = 0;
    int16_t  H2 = 0, H4 = 0, H5 = 0;
    int8_t   H6 = 0;
};

}

#endif
