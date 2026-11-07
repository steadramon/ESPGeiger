/*
  WifiStable.h - how long WiFi has been up

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
#ifndef CORIUM_WIFISTABLE_H
#define CORIUM_WIFISTABLE_H

#include <stdint.h>
#include "Corium/System/FastMillis.h"

namespace corium {
namespace Wifi {
  extern uint32_t connectedAtMs;  // 0 when down
  extern bool settled;   // set once up past 2^31 ms, stays set over the wrap

  // Lets AsyncTCP drain after a reconnect
  inline bool stableFor(uint32_t ms) {
    return connectedAtMs != 0 && (settled || (uint32_t)(fastMillis() - connectedAtMs) >= ms);
  }
}
}  // namespace corium

#endif
