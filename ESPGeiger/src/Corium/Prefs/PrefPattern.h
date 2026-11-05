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
#ifndef CORIUM_PREFPATTERN_H
#define CORIUM_PREFPATTERN_H

// Pattern is literal chars (x? optional), then [..] or \d with + * {m} {m,} {m,n}, then the end.
// p in SRAM or PROGMEM, nullptr = no pattern. Returns 1 on a match (empty always matches),
// 0 if not, -1 if p isn't that shape
int prefPattern(const char* p, const char* value);

#endif
