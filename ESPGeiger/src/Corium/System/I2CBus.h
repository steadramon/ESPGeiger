/*
  I2CBus.h - one Wire.begin, shared by the OLED and env sensors

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
#ifndef EG_I2CBUS_H
#define EG_I2CBUS_H

#include <stdint.h>

// i2c.sda/scl defaults: a board's OLED_SDA/OLED_SCL, the OLED's 4/5, else per chip
#if defined(OLED_SDA)
  #define I2C_DEFAULT_SDA OLED_SDA
  #define I2C_DEFAULT_SCL OLED_SCL
#elif defined(SSD1306_DISPLAY)
  #define I2C_DEFAULT_SDA 4
  #define I2C_DEFAULT_SCL 5
#elif defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S2)
  #define I2C_DEFAULT_SDA 8
  #define I2C_DEFAULT_SCL 9
#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
  #define I2C_DEFAULT_SDA 6
  #define I2C_DEFAULT_SCL 7
#elif defined(ESP32)
  #define I2C_DEFAULT_SDA 21
  #define I2C_DEFAULT_SCL 22
#else
  #define I2C_DEFAULT_SDA 4
  #define I2C_DEFAULT_SCL 5
#endif

namespace I2CBus {
  void pins(uint8_t sda, uint8_t scl);   // whoever owns the pin prefs, no effect once up
  bool begin();   // first user starts it, a remap would freeze the others
}

#endif
