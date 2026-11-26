/*
  RegistryInfo.cpp - what /ps reads of each slot

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
#include "Registry.h"
#include "Corium/Net/Wifi.h"
#include "Corium/System/Clock.h"

// Not in Registry.cpp, a second caller there stops walk() inlining Wifi::stableFor

// the same gates as unmet_requirements()
const char* corium::Registry::state(uint8_t idx) {
  if (idx >= _count) return nullptr;
  uint8_t f = _slots[idx].flags;
  if (f & FLAG_QUARANTINED) return PSTR("quarantined");
  if (f & FLAG_OFF) return PSTR("off");
  if ((f & FLAG_REQUIRES_WIFI) && !corium::Wifi::stableFor(WIFI_SETTLE_MS)) return PSTR("wifi");
  if ((f & FLAG_REQUIRES_NTP) && !corium::Clock::synced) return PSTR("ntp");
  return nullptr;
}
