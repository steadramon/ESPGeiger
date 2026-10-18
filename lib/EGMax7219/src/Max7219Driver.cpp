/*
  Max7219Driver.cpp - Minimal bit-banged MAX7219 driver (u8g2-free).

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
#include "Max7219Driver.h"

void Max7219Driver::begin(int8_t din, int8_t clk, int8_t cs, uint8_t chips,
                          uint8_t decode, uint8_t scanlimit) {
  _din = din; _clk = clk; _cs = cs;
  _chips = chips < 1 ? 1 : chips > MAX_CHIPS ? MAX_CHIPS : chips;
  if (_din < 0 || _clk < 0 || _cs < 0) { _ready = false; return; }

  pinMode(_din, OUTPUT);
  pinMode(_clk, OUTPUT);
  pinMode(_cs,  OUTPUT);
  digitalWrite(_clk, LOW);
  digitalWrite(_cs,  HIGH);
  _ready = true;
  _decode = decode;
  _scan = scanlimit & 0x07;
  // digits power up random; 0x0F is blank when decoded
  _known = false;
  for (uint8_t d = 0; d < 8; d++)
    writeAll(REG_DIGIT0 + d, ((_decode >> d) & 1) ? 0x0F : 0x00);
  _known = true;
  _off = false;
  refresh();
}

void Max7219Driver::sendSetup(uint8_t i) {
  switch (i) {
    case 0:  writeAll(REG_DISPLAYTEST, 0x00); break;   // normal
    case 1:  writeAll(REG_SCANLIMIT, _scan); break;
    case 2:  writeAll(REG_DECODEMODE, _decode); break;
    case 3:  writeAll(REG_INTENSITY, _level); break;
    default: writeAll(REG_SHUTDOWN, _off ? 0x00 : 0x01); break;
  }
}

void Max7219Driver::refresh() {
  for (uint8_t i = 0; i < SETUP_REGS; i++) sendSetup(i);
}

// from _rows, sent even if the same
void Max7219Driver::sendRow(uint8_t r) {
  digitalWrite(_cs, LOW);
  for (uint8_t k = 0; k < _chips; k++) shiftFrame(REG_DIGIT0 + r, _rows[r][k]);
  digitalWrite(_cs, HIGH);
}

void Max7219Driver::heal() {
  if (!_ready) return;
  sendSetup(_heal_reg);
  if (++_heal_reg >= SETUP_REGS) _heal_reg = 0;
  sendRow(_heal_row);
  if (++_heal_row > _scan) _heal_row = 0;
}

// MSB first, no latch
void Max7219Driver::shiftFrame(uint8_t reg, uint8_t val) {
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(_din, (reg & 0x80) ? HIGH : LOW);
    reg <<= 1;
    digitalWrite(_clk, HIGH);
    digitalWrite(_clk, LOW);
  }
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(_din, (val & 0x80) ? HIGH : LOW);
    val <<= 1;
    digitalWrite(_clk, HIGH);
    digitalWrite(_clk, LOW);
  }
}

static bool is_digit(uint8_t reg) {
  return reg >= Max7219Driver::REG_DIGIT0 && reg < Max7219Driver::REG_DIGIT0 + 8;
}

// farthest chip first
void Max7219Driver::write(uint8_t chip, uint8_t reg, uint8_t val) {
  if (!_ready || chip >= _chips) return;
  uint8_t k = (uint8_t)(_chips - 1 - chip);
  if (is_digit(reg)) {
    uint8_t& held = _rows[reg - REG_DIGIT0][k];
    if (held == val) return;
    held = val;
  }
  digitalWrite(_cs, LOW);
  for (uint8_t j = 0; j < _chips; j++) {
    if (j == k) shiftFrame(reg, val);
    else        shiftFrame(REG_NOOP, 0x00);
  }
  digitalWrite(_cs, HIGH);
}

void Max7219Driver::writeAll(uint8_t reg, uint8_t val) {
  if (!_ready) return;
  if (is_digit(reg)) {
    uint8_t* row = _rows[reg - REG_DIGIT0];
    bool same = _known;
    for (uint8_t k = 0; k < _chips; k++) {
      if (row[k] != val) same = false;
      row[k] = val;
    }
    if (same) return;
  }
  digitalWrite(_cs, LOW);
  for (uint8_t j = 0; j < _chips; j++) shiftFrame(reg, val);
  digitalWrite(_cs, HIGH);
}

void Max7219Driver::writeDigit(uint8_t index, uint8_t code, bool dp) {
  if (index > 7) return;
  write(0, REG_DIGIT0 + index, (uint8_t)(code | (dp ? 0x80 : 0)));   // DP = bit 7
}

// FC-16: digits are rows; Generic: digits are columns, reversed; upside down
// also runs the chain backwards
static bool lit(const uint8_t* cols, uint16_t w, uint8_t p, uint8_t r, uint8_t i,
                uint8_t orient) {
  uint16_t x;
  uint8_t  y;
  switch (orient) {
    case 1:  x = p * 8 + 7 - r;         y = i;     break;   // Generic
    case 2:  x = w - 1 - (p * 8 + i);   y = 7 - r; break;   // FC-16 upside down
    case 3:  x = w - 8 - p * 8 + r;     y = 7 - i; break;   // Generic upside down
    default: x = p * 8 + i;             y = r;     break;   // FC-16
  }
  return cols[x] & (1 << y);
}

// left panel first, MSB = left column
void Max7219Driver::drawMatrix(const uint8_t* cols, uint8_t orient) {
  if (!_ready) return;
  uint16_t w = (uint16_t)_chips * 8;
  for (uint8_t r = 0; r < 8; r++) {
    bool same = _known;
    for (uint8_t p = 0; p < _chips; p++) {
      uint8_t v = 0;
      for (uint8_t i = 0; i < 8; i++)
        if (lit(cols, w, p, r, i, orient)) v |= (uint8_t)(0x80 >> i);
      if (v != _rows[r][p]) same = false;
      _rows[r][p] = v;
    }
    if (!same) sendRow(r);
  }
}

void Max7219Driver::setIntensity(uint8_t intensity) {
  _level = intensity > 15 ? 15 : intensity;
  writeAll(REG_INTENSITY, _level);
}

void Max7219Driver::setDecode(uint8_t mask) {
  _decode = mask;
  writeAll(REG_DECODEMODE, mask);
}

void Max7219Driver::shutdown(bool off) {
  _off = off;
  writeAll(REG_SHUTDOWN, off ? 0x00 : 0x01);
}
