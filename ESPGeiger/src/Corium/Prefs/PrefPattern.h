/*
  PrefPattern.h - The part of an HTML pattern the device checks itself.

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
#ifndef PREFPATTERN_H
#define PREFPATTERN_H

// literal chars (x? optional), then [..] or \d with + * {m} {m,} {m,n}, then the end.
// p SRAM or PROGMEM, nullptr = none. 1 match (empty always), 0 no, -1 not that shape
int pref_pattern(const char* p, const char* value);

#endif
