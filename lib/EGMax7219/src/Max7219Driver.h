/*
  Max7219Driver.h - Minimal bit-banged MAX7219 driver (u8g2-free).

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
#ifndef EG_MAX7219DRIVER_H
#define EG_MAX7219DRIVER_H
#include <Arduino.h>

// write only, no detect
class Max7219Driver {
  public:
    enum : uint8_t {
      REG_NOOP        = 0x00,
      REG_DIGIT0      = 0x01,   // DIGIT0..7 = 0x01..0x08
      REG_DECODEMODE  = 0x09,
      REG_INTENSITY   = 0x0A,
      REG_SCANLIMIT   = 0x0B,
      REG_SHUTDOWN    = 0x0C,
      REG_DISPLAYTEST = 0x0F,
    };

    // decode 0xFF BCD, 0x00 raw, 0x0F low 4 BCD
    void begin(int8_t din, int8_t clk, int8_t cs, uint8_t chips,
               uint8_t decode, uint8_t scanlimit);

    // skips digits the chip already holds
    void write(uint8_t chip, uint8_t reg, uint8_t val);   // chip 0 = nearest
    void writeAll(uint8_t reg, uint8_t val);

    void writeDigit(uint8_t index, uint8_t code, bool dp);

    // byte per column, bit y = row y, orient 0..3 = 0/90/180/270
    void drawMatrix(const uint8_t* cols, uint8_t orient);

    static constexpr uint8_t MAX_CHIPS = 8;

    void setIntensity(uint8_t intensity);   // 0..15
    void setDecode(uint8_t mask);
    void shutdown(bool off);
    void refresh();   // every setup register again
    // one setup register and one row a call
    void heal();

    uint8_t chips() const { return _chips; }
    bool ready() const { return _ready; }

  private:
    static constexpr uint8_t SETUP_REGS = 5;
    void shiftFrame(uint8_t reg, uint8_t val);
    void sendSetup(uint8_t i);
    void sendRow(uint8_t r);

    int8_t  _din = -1, _clk = -1, _cs = -1;
    uint8_t _chips = 1;
    bool    _ready = false;
    bool    _known = false;   // _rows is what the chips hold
    bool    _off = false;
    uint8_t _decode = 0, _scan = 7, _level = 0;
    uint8_t _heal_reg = 0, _heal_row = 0;
    uint8_t _rows[8][MAX_CHIPS];   // [digit][shift order, farthest first]
};

#endif
