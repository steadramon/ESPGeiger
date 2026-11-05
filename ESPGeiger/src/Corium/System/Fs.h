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

namespace corium {
namespace Fs {
  bool begin();   // first call only, formats after 3 failed mounts
  // all mount first and check before touching: esp32 logs a missing file
  File openR(const char* path);   // empty File if not there
  File openW(const char* path);
  bool remove(const char* path);   // false if not there
  bool exists(const char* path);
  bool rename(const char* from, const char* to);
  bool mkdir(const char* path);
  void touch(const char* path);

  // flash paths, copied for the call; longer than PATH_P_MAX fails
  static const size_t PATH_P_MAX = 31;
  File openR(const __FlashStringHelper* path);
  File openW(const __FlashStringHelper* path);
  bool remove(const __FlashStringHelper* path);
  bool exists(const __FlashStringHelper* path);
  bool rename(const __FlashStringHelper* from, const __FlashStringHelper* to);
}
}  // namespace corium

#endif
