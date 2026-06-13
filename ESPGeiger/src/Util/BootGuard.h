/*
  BootGuard.h - RTC-backed boot-loop counter for safety telemetry.

  Increments a counter on every boot at the very top of setup(), cleared
  after the device proves healthy via mark_ok(). Survives soft reset
  (watchdog, panic, exception), wiped by cold power-cycle.

  v0.12.0 is log-only: the counter is exposed in the WebAPI handshake
  as field `bc` so the fleet's crash-loop signature is visible without
  any local action. Future versions add a threshold-triggered recovery
  action.

  RTC layout: 4 bytes at user-memory word offset 40 (CrashDump owns
  0..39; see CrashDump.cpp).

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
#ifndef _ESPG_BOOTGUARD_H
#define _ESPG_BOOTGUARD_H

#include <stdint.h>

namespace BootGuard {

// First thing in setup(). Increments the persistent counter.
void on_boot();

// Call once the device has run healthy for ~30 s with WiFi stable.
// Clears the counter so the next boot starts from zero.
void mark_ok();

// Current counter value. Read by WebAPI handshake to send as `bc`.
uint8_t boot_count();

} // namespace BootGuard

#endif
