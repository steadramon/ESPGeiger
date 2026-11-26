/*
  Layout.h - the 4 MB esp32 partition table, and the move to bigger app slots

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
#ifndef CORIUM_LAYOUT_H
#define CORIUM_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

namespace corium {
// old = default.csv, new = min_spiffs.csv, app0 doesn't move.
// The move: boot app0, files into app1, new table, restart, format, files back
namespace Layout {
  enum Kind : uint8_t { UNKNOWN, OLD, NEW };
  // kept in nvs, a reset picks up from here
  enum Step : uint8_t { IDLE, SAVING, SAVED, RESTORING, CLEARING };

  static const uint32_t TABLE_AT = 0x8000;
  static const size_t SECTOR = 4096;
  static const size_t TABLE_LEN = 0xC00;   // what the bootloader reads
  static const uint32_t BACKUP_AT = 0x200000;   // inside app1 in both tables
  static const uint32_t BACKUP_MAX = 0x40000;

  Kind kind();   // the table in flash
  Step step();
  uint8_t slot();   // the app slot running
  const char* check();   // flash string why not, nullptr = ok to start
  const char* start();   // check() then restarts into the move, else why not
  void resume();   // Boot::prefs, before the fs mounts
  void rescue();   // the fs didn't mount: the old table's files found = restarts into the put back
  void build(uint8_t* sector);   // the new table as the sector holds it
}
}  // namespace corium

#endif
