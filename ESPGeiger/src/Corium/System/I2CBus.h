/*
  I2CBus.h - one Wire.begin, shared by the modules

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
#ifndef CORIUM_I2CBUS_H
#define CORIUM_I2CBUS_H

#include <stdint.h>
#include "App/AppConfig.h"

// i2c.sda/scl defaults: the app's, else per chip
#ifndef I2C_DEFAULT_SDA
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S2)
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
#endif

// 8266 wait per bit on a held SCL, the core's 150 ms stalls a dead bus for seconds
#ifndef CORIUM_I2C_STRETCH_US
#define CORIUM_I2C_STRETCH_US 2000
#endif

namespace corium {
namespace I2CBus {
  void pins(uint8_t sda, uint8_t scl);   // whoever owns the pin prefs, no effect once up
  bool begin();   // first user starts it, a remap would freeze the others
}
}  // namespace corium

#endif
