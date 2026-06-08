/*
  AsairAHT.cpp - Lean Asair AHT10 / AHT20 driver.

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
#include "AsairAHT.h"
#include <math.h>

namespace AsairAHT {

static constexpr uint8_t ADDR        = 0x38;
static constexpr uint8_t CMD_STATUS  = 0x71;
static constexpr uint8_t STATUS_CAL  = 0x08;   // bit3: calibrated
static constexpr uint8_t STATUS_BUSY = 0x80;   // bit7: measurement in flight

static const uint8_t INIT_SEQ[3]    = { 0xBE, 0x08, 0x00 };
static const uint8_t TRIGGER_SEQ[3] = { 0xAC, 0x33, 0x00 };

static uint8_t crc8(const uint8_t* buf, uint8_t len) {
  // Sensirion-style: poly 0x31, init 0xFF, MSB-first, no xor-out.
  uint8_t c = 0xFF;
  for (uint8_t i = 0; i < len; i++) {
    c ^= buf[i];
    for (uint8_t b = 0; b < 8; b++) {
      c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
    }
  }
  return c;
}

bool Sensor::status(uint8_t& s) {
  _wire->beginTransmission(ADDR);
  _wire->write(CMD_STATUS);
  if (_wire->endTransmission(false) != 0) return false;
  if (_wire->requestFrom((int)ADDR, 1) != 1) return false;
  if (!_wire->available()) return false;
  s = (uint8_t)_wire->read();
  return true;
}

bool Sensor::sendInit() {
  _wire->beginTransmission(ADDR);
  _wire->write(INIT_SEQ, sizeof(INIT_SEQ));
  return _wire->endTransmission() == 0;
}

bool Sensor::trigger() {
  _wire->beginTransmission(ADDR);
  _wire->write(TRIGGER_SEQ, sizeof(TRIGGER_SEQ));
  return _wire->endTransmission() == 0;
}

bool Sensor::begin(TwoWire& wire) {
  _wire = &wire;
  _present = false;
  // Datasheet calls for ~40 ms after power-on before first command.
  // The host has typically been up for much longer by now (modules init
  // late in EGModule begin()), but small belt-and-braces sleep stays safe.
  delay(20);
  uint8_t s = 0;
  if (!status(s)) return false;
  if ((s & STATUS_CAL) == 0) {
    if (!sendInit()) return false;
    delay(10);
    if (!status(s)) return false;
    if ((s & STATUS_CAL) == 0) return false;
  }
  _present = true;
  return true;
}

bool Sensor::readResult(float& t_c, float& h_pct, float& p_hpa) {
  p_hpa = NAN;
  if (!_present) return false;
  uint8_t buf[7];
  if (_wire->requestFrom((int)ADDR, 7) != 7) return false;
  for (uint8_t i = 0; i < 7; i++) {
    if (!_wire->available()) return false;
    buf[i] = (uint8_t)_wire->read();
  }
  if (buf[0] & STATUS_BUSY) return false;
  if (crc8(buf, 6) != buf[6]) return false;

  uint32_t h_raw = ((uint32_t)buf[1] << 12) | ((uint32_t)buf[2] << 4) | (buf[3] >> 4);
  uint32_t t_raw = (((uint32_t)buf[3] & 0x0F) << 16) | ((uint32_t)buf[4] << 8) | buf[5];

  // Datasheet linearisation. 2^20 = 1048576.
  static constexpr float INV_2P20 = 1.0f / 1048576.0f;
  h_pct = (float)h_raw * INV_2P20 * 100.0f;
  t_c   = (float)t_raw * INV_2P20 * 200.0f - 50.0f;
  if (h_pct < 0.0f)   h_pct = 0.0f;
  if (h_pct > 100.0f) h_pct = 100.0f;
  return true;
}

bool Sensor::read(float& t_c, float& h_pct, float& p_hpa) {
  if (!_present || !trigger()) return false;
  delay(80);
  return readResult(t_c, h_pct, p_hpa);
}

}
