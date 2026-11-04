/*
  HwKey.h - the chip's own signing key (DS peripheral), blob kept in nvs.

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

#ifndef HWKEY_H
#define HWKEY_H

#include <stddef.h>
#include <stdint.h>

// RSA-2048, PKCS#1 v1.5 SHA-256; the key and blob are put there before the board ships
namespace HwKey {
  static constexpr size_t SIG = 256;
  static constexpr size_t HASH = 32;
  static constexpr size_t BLOB = 1220;   // ets_ds_data_t

  // EM as the DS takes it, little endian words
  void pad(const uint8_t hash[HASH], uint8_t out[SIG]);

  bool present();
  bool burned();   // KEY5 is ours, blob or not
  bool sign(const uint8_t hash[HASH], uint8_t sig[SIG]);   // big endian
  bool put(const uint8_t blob[BLOB]);   // kept only if it signs
}

#endif
