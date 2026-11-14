/*
  Attr.h - compiler attributes, one name for both cores

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
#ifndef CORIUM_ATTR_H
#define CORIUM_ATTR_H

// A dropped result warns. esp32 is gnu++11, so no [[nodiscard]]
#define CORIUM_NODISCARD __attribute__((warn_unused_result))

// The 1 s tick. esp32 runs it from iram with the helpers inlined,
// the 8266 runs it from flash with the helpers out of line, so only real work fetches them
#ifdef ESP32
#define CORIUM_TICK_ATTR IRAM_ATTR
#define CORIUM_TICK_COLD inline
#else
#define CORIUM_TICK_ATTR
#define CORIUM_TICK_COLD __attribute__((noinline))
#endif

#endif
