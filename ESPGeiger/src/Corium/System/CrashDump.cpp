/*
  CrashDump.cpp - the reset record, both chips

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
#include "CrashDump.h"
#include "StatusWord.h"
#include <Arduino.h>

namespace corium {
namespace CrashDump {

static Reset s_reset;

static const char KINDS[] PROGMEM =
  "Unknown\0Power on\0External reset\0Software restart\0Deep sleep wake\0"
  "Brownout\0Exception\0Hardware watchdog\0Software watchdog\0Interrupt watchdog\0Task watchdog\0Watchdog\0"
  "Abort\0Out of memory\0Stack smash\0Crash\0";

const char* kindText(uint8_t kind) {
  const char* s = KINDS;
  for (uint8_t i = 0; i < kind && pgm_read_byte(s); i++) s += strlen_P(s) + 1;
  return pgm_read_byte(s) ? s : KINDS;
}

const Reset& reset() { return s_reset; }

Reset& fresh(uint8_t kind) {
  s_reset = Reset();
  s_reset.kind = kind;
  return s_reset;
}

// Any crash sets the health bit, dump or not
void settle() {
  s_reset.crash = s_reset.kind >= K_BROWNOUT || s_reset.damaged;
  if (s_reset.crash) healthWord().set(DEV_CRASHED, true);
}

} // namespace CrashDump
}  // namespace corium
