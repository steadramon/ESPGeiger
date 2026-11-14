/*
  BootGuard.h - safe mode after three quick crashes

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
#ifndef CORIUM_BOOTGUARD_H
#define CORIUM_BOOTGUARD_H

#include <stdint.h>
#include "App/AppConfig.h"

#ifndef CORIUM_BOOTGUARD_LIVED_S
#define CORIUM_BOOTGUARD_LIVED_S 600
#endif
#ifndef CORIUM_BOOTGUARD_STABLE_MS
#define CORIUM_BOOTGUARD_STABLE_MS 5000
#endif

namespace corium {
namespace BootGuard {

constexpr uint32_t LIVED_S   = CORIUM_BOOTGUARD_LIVED_S;     // from beginAll
constexpr uint32_t STABLE_MS = CORIUM_BOOTGUARD_STABLE_MS;   // wifi
constexpr uint32_t DAY_MS    = 86400000UL;

void onBoot();      // first thing in setup()
void markLived();   // up LIVED_S, a crash after doesn't count
void markDay();     // up DAY_MS
void requestSafe();   // safe mode on next restart
void requestPortal();   // next boot: the portal if the saved wifi fails, offline builds too

bool     safeMode();
bool     entered();       // first safe boot
uint32_t retryAfterMs();   // ms
uint8_t  crashStreak();
bool     portalAsked();

}
}  // namespace corium

#endif
