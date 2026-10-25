/*
  KeyStore.h - small keys kept outside LittleFS, so a format keeps them.

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

#ifndef KEYSTORE_H
#define KEYSTORE_H

#include <stddef.h>
#include <stdint.h>

// esp32 nvs, esp8266 the eeprom sector
namespace KeyStore {
  static constexpr size_t MAX = 32;
  static constexpr uint8_t SLOTS = 2;   // 0 = the key
  bool read(uint8_t* out, size_t len, uint8_t slot = 0);   // false = none, or not len bytes
  bool write(const uint8_t* in, size_t len, uint8_t slot = 0);
  void clear(uint8_t slot = 0);
}

#endif
