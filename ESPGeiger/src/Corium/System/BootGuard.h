/*
  BootGuard.h - three crashes in a row, each before LIVED_S, and the next boot runs safe mode.

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
#ifndef ESPG_BOOTGUARD_H
#define ESPG_BOOTGUARD_H

#include <stdint.h>

namespace BootGuard {

constexpr uint32_t LIVED_S   = 600;    // from begin_all
constexpr uint32_t STABLE_MS = 5000;   // wifi
constexpr uint32_t DAY_MS    = 86400000UL;

void on_boot();      // first thing in setup()
void mark_lived();   // up LIVED_S, a crash after doesn't count
void mark_day();     // up DAY_MS
void request_safe();   // safe mode on next restart

bool     safe_mode();
bool     entered();       // first safe boot
uint32_t retry_after();   // ms
uint8_t  crash_streak();

}

#endif
