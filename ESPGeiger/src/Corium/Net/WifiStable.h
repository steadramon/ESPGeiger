/*
  WifiStable.h - how long WiFi has been up, apart from the rest of Wifi.h
  so host tests use the same rule.

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
#ifndef UTIL_WIFISTABLE_H
#define UTIL_WIFISTABLE_H

#include <stdint.h>
#include "Corium/System/FastMillis.h"

namespace Wifi {
  extern uint32_t connected_at_ms;  // 0 when down

  // lets AsyncTCP drain after a reconnect
  inline bool stable_for(uint32_t ms) {
    return connected_at_ms != 0 && (uint32_t)(fast_millis() - connected_at_ms) >= ms;
  }
}

#endif
