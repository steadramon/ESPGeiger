/*
  EGTimeZone.h - Olson name to POSIX TZ rule.
  
  Copyright (C) 2023 @steadramon

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
#ifndef EGTIMEZONE_H
#define EGTIMEZONE_H

namespace EGTimeZone {

// Olson name ("Europe/London") to the POSIX rule setenv("TZ", ...) wants
// ("GMT0BST,M3.5.0/1,M10.5.0"). Returns PROGMEM, valid for the program's life.
// nullptr on a miss, so a tzdb rename cannot pass silently.
const char *posixFor(const char *olson);

}

#endif
