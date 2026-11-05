/*
  Debounce.h - A level that must hold steady before it counts.

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

#ifndef CORIUM_DEBOUNCE_H
#define CORIUM_DEBOUNCE_H

#include <stdint.h>

namespace corium {
// step() returns true when the steady level flips
struct Debounce {
  bool     read  = false;   // Last raw level
  bool     level = false;   // steady level
  uint32_t since = 0;       // Last raw change

  void reset(bool on, uint32_t now) {
    read = level = on;
    since = now;
  }

  bool step(bool on, uint32_t now, uint32_t ms) {
    if (on != read) {
      read = on;
      since = now;
      return false;
    }
    if (on == level || now - since < ms) return false;
    level = on;
    return true;
  }
};
}  // namespace corium

#endif
