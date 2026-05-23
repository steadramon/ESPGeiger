/*
  BoschTHP.cpp - Lean BME280 / BMP280 driver.

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
#include "BoschTHP.h"
#include <math.h>

namespace BoschTHP {

// Bosch BME280 / BMP280 register map
static constexpr uint8_t REG_CHIPID    = 0xD0;
static constexpr uint8_t REG_RESET     = 0xE0;
static constexpr uint8_t REG_CTRL_HUM  = 0xF2;   // BME280 only
static constexpr uint8_t REG_CTRL_MEAS = 0xF4;
static constexpr uint8_t REG_CONFIG    = 0xF5;
static constexpr uint8_t REG_DATA      = 0xF7;   // press(3) temp(3) hum(2)
static constexpr uint8_t REG_CAL_TP    = 0x88;   // 24 B  T1..T3 + P1..P9 LE
static constexpr uint8_t REG_CAL_H1    = 0xA1;   // 1 B
static constexpr uint8_t REG_CAL_H2_6  = 0xE1;   // 7 B

bool Sensor::begin(TwoWire& wire, uint8_t addr) {
  _wire = &wire;
  _chip = CHIP_NONE;
  if (addr) {
    if (!probe(addr)) return false;
  } else {
    if (!probe(0x76) && !probe(0x77)) return false;
  }
  // Soft reset; chip needs ~2 ms before NVM copy completes.
  write8(REG_RESET, 0xB6);
  delay(5);
  if (!readCal()) { _chip = CHIP_NONE; return false; }
  if (!configure()) { _chip = CHIP_NONE; return false; }
  return true;
}

bool Sensor::probe(uint8_t addr) {
  _addr = addr;
  uint8_t id = 0;
  if (!read8(REG_CHIPID, id)) return false;
  if (id == CHIP_BME280 || id == CHIP_BMP280) {
    _chip = (ChipId)id;
    return true;
  }
  return false;
}

bool Sensor::readCal() {
  uint8_t buf[26];
  if (!readN(REG_CAL_TP, buf, 24)) return false;
  // All little-endian, signed where appropriate per datasheet.
  T1 = (uint16_t)(buf[0]  | (buf[1]  << 8));
  T2 = (int16_t) (buf[2]  | (buf[3]  << 8));
  T3 = (int16_t) (buf[4]  | (buf[5]  << 8));
  P1 = (uint16_t)(buf[6]  | (buf[7]  << 8));
  P2 = (int16_t) (buf[8]  | (buf[9]  << 8));
  P3 = (int16_t) (buf[10] | (buf[11] << 8));
  P4 = (int16_t) (buf[12] | (buf[13] << 8));
  P5 = (int16_t) (buf[14] | (buf[15] << 8));
  P6 = (int16_t) (buf[16] | (buf[17] << 8));
  P7 = (int16_t) (buf[18] | (buf[19] << 8));
  P8 = (int16_t) (buf[20] | (buf[21] << 8));
  P9 = (int16_t) (buf[22] | (buf[23] << 8));
  if (_chip != CHIP_BME280) return true;
  // Humidity cal lives in two non-contiguous chunks.
  if (!read8(REG_CAL_H1, H1)) return false;
  if (!readN(REG_CAL_H2_6, buf, 7)) return false;
  H2 = (int16_t)(buf[0] | (buf[1] << 8));
  H3 = buf[2];
  // H4 / H5 are packed nibbles. Datasheet 4.2.2 table 16.
  H4 = (int16_t)((((int8_t)buf[3]) << 4) | (buf[4] & 0x0F));
  H5 = (int16_t)((((int8_t)buf[5]) << 4) | (buf[4] >> 4));
  H6 = (int8_t)buf[6];
  return true;
}

bool Sensor::configure() {
  // BME280 humidity oversample must be set before ctrl_meas latches.
  if (_chip == CHIP_BME280) {
    if (!write8(REG_CTRL_HUM, 0x01)) return false;       // osrs_h x1
  }
  if (!write8(REG_CTRL_MEAS, 0x27)) return false;
  if (!write8(REG_CONFIG, 0x60)) return false;
  return true;
}

bool Sensor::read(float& t_c, float& h_pct, float& p_hpa) {
  if (_chip == CHIP_NONE) return false;
  uint8_t buf[8];
  uint8_t want = (_chip == CHIP_BME280) ? 8 : 6;
  if (!readN(REG_DATA, buf, want)) return false;
  int32_t adc_P = ((int32_t)buf[0] << 12) | ((int32_t)buf[1] << 4) | (buf[2] >> 4);
  int32_t adc_T = ((int32_t)buf[3] << 12) | ((int32_t)buf[4] << 4) | (buf[5] >> 4);
  int32_t adc_H = (_chip == CHIP_BME280)
                  ? (((int32_t)buf[6] << 8) | buf[7]) : 0;

  // Temperature - Bosch reference formula in float.
  float v1 = ((float)adc_T / 16384.0f - (float)T1 / 1024.0f) * (float)T2;
  float v2 = ((float)adc_T / 131072.0f - (float)T1 / 8192.0f);
  v2 = v2 * v2 * (float)T3;
  float t_fine = v1 + v2;
  t_c = t_fine * (1.0f / 5120.0f);

  // Pressure - returns Pa, we hand back hPa.
  v1 = (t_fine * 0.5f) - 64000.0f;
  v2 = v1 * v1 * (float)P6 * (1.0f / 32768.0f);
  v2 = v2 + v1 * (float)P5 * 2.0f;
  v2 = (v2 * 0.25f) + ((float)P4 * 65536.0f);
  v1 = ((float)P3 * v1 * v1 * (1.0f / 524288.0f) + (float)P2 * v1) * (1.0f / 524288.0f);
  v1 = (1.0f + v1 * (1.0f / 32768.0f)) * (float)P1;
  if (v1 == 0.0f) { p_hpa = 0.0f; }
  else {
    float p = 1048576.0f - (float)adc_P;
    p = (p - (v2 * (1.0f / 4096.0f))) * 6250.0f / v1;
    v1 = (float)P9 * p * p * (1.0f / 2147483648.0f);
    v2 = p * (float)P8 * (1.0f / 32768.0f);
    p = p + (v1 + v2 + (float)P7) * (1.0f / 16.0f);
    p_hpa = p * 0.01f;
  }

  // Humidity - BME280 only.
  if (_chip == CHIP_BME280) {
    float h = t_fine - 76800.0f;
    h = ((float)adc_H - ((float)H4 * 64.0f + (float)H5 * (1.0f / 16384.0f) * h))
        * ((float)H2 * (1.0f / 65536.0f)
           * (1.0f + (float)H6 * (1.0f / 67108864.0f) * h
                   * (1.0f + (float)H3 * (1.0f / 67108864.0f) * h)));
    h = h * (1.0f - (float)H1 * h * (1.0f / 524288.0f));
    if (h > 100.0f) h = 100.0f;
    else if (h < 0.0f) h = 0.0f;
    h_pct = h;
  } else {
    h_pct = NAN;
  }
  return true;
}

const __FlashStringHelper* Sensor::chipName() const {
  if (_chip == CHIP_BME280) return F("BME280");
  if (_chip == CHIP_BMP280) return F("BMP280");
  return nullptr;
}

bool Sensor::read8(uint8_t reg, uint8_t& out) {
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) return false;
  if (_wire->requestFrom((int)_addr, 1) != 1) return false;
  if (!_wire->available()) return false;
  out = (uint8_t)_wire->read();
  return true;
}

bool Sensor::readN(uint8_t reg, uint8_t* out, uint8_t len) {
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) return false;
  if (_wire->requestFrom((int)_addr, (int)len) != (int)len) return false;
  for (uint8_t i = 0; i < len; i++) {
    if (!_wire->available()) return false;
    out[i] = (uint8_t)_wire->read();
  }
  return true;
}

bool Sensor::write8(uint8_t reg, uint8_t val) {
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  _wire->write(val);
  return _wire->endTransmission() == 0;
}

} 
