/*
  BootButton.cpp - Button held at boot: offline, or factory reset if held long.

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
#include <Arduino.h>
#include "Corium/System/BootButton.h"
#include "Corium/System/FastMillis.h"

#define BB_STR_(x) #x
#define BB_STR(x)  BB_STR_(x)

static const uint16_t POLL_MS   = 50;
static const uint16_t SETTLE_MS = 500;   // after the release
static const uint32_t STUCK_MS  = CORIUM_BOOT_BUTTON_STUCK_S * 1000UL;

corium::BootButton::Hold corium::BootButton::check(bool (*pressed)(), void (*held)(), void (*left)(int s)) {
  if (!pressed()) return Hold::NONE;

  Serial.println(F("BOOT: button held - offline mode, hold " BB_STR(CORIUM_BOOT_BUTTON_RESET_S) "s for factory reset"));
  held();
  uint32_t start = corium::fastMillis();
  int last_shown = 999;
  bool armed = false;

  while (pressed()) {
    uint32_t held_ms = corium::fastMillis() - start;
    if (held_ms >= STUCK_MS) {
      Serial.println(F("BOOT: button stuck - ignored"));
      return Hold::NONE;
    }
    int remain = CORIUM_BOOT_BUTTON_RESET_S - (int)(held_ms / 1000);
    if (remain != last_shown) {
      last_shown = remain;
      if (remain >= 1) {
        left(remain);
      } else if (!armed) {
        armed = true;
        left(0);
        Serial.println(F("BOOT: factory reset armed"));
      }
    }
    delay(POLL_MS);
  }
  delay(SETTLE_MS);
  return armed ? Hold::FULL_RESET : Hold::OFFLINE;
}
