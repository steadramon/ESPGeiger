/*
  Fs.h - LittleFS, mounted once and left mounted

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
#ifndef CORIUM_FS_H
#define CORIUM_FS_H

#include <LittleFS.h>
#include "Corium/Util/Attr.h"

namespace corium {
namespace Fs {
  CORIUM_NODISCARD bool begin();   // first call only, formats after 3 failed mounts
  void end();     // A new fs image goes under it, no more fs use until reboot
  // These all mount first and check before touching, the esp32 logs a missing file
  File openR(const char* path);   // empty File if not there
  File openW(const char* path);
  CORIUM_NODISCARD bool remove(const char* path);   // true = not there now, including if it never was
  bool exists(const char* path);
  CORIUM_NODISCARD bool rename(const char* from, const char* to);
  bool mkdir(const char* path);
  void touch(const char* path);
  // All of buf or no file, the size is read back to check
  CORIUM_NODISCARD bool writeWhole(const char* path, const void* buf, size_t len);
  // Walks the fs, so call from a page load not the loop. false on a host
  bool usage(uint32_t* used, uint32_t* total);

  // Block erases the writes cost, an estimate kept in /wear
  uint32_t wear();       // since 0.14
  uint32_t wearBoot();   // since this boot
  bool wearPct100(uint32_t* x100);   // hundredths of a % of the rated erases, false on a host
  void saveWear();       // at shutdown, else every few erases

  // Flash paths get copied to RAM for the call, longer than PATH_P_MAX fails
  static const size_t PATH_P_MAX = 31;
  File openR(const __FlashStringHelper* path);
  File openW(const __FlashStringHelper* path);
  CORIUM_NODISCARD bool remove(const __FlashStringHelper* path);
  bool exists(const __FlashStringHelper* path);
  CORIUM_NODISCARD bool rename(const __FlashStringHelper* from, const __FlashStringHelper* to);
  CORIUM_NODISCARD bool writeWhole(const __FlashStringHelper* path, const void* buf, size_t len);
}
}  // namespace corium

#endif
