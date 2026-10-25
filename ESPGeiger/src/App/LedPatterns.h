/*
  LedPatterns.h - ESPGeiger's status LED patterns, LedSignal plays them.

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
#ifndef APP_LED_PATTERNS_H
#define APP_LED_PATTERNS_H

#include "Corium/Platform/Flash.h"
#include "App/AppConfig.h"
#include "Corium/Output/LedSignal.h"

enum : uint8_t { LED_ACTIVITY, LED_ACK, LED_DISP_ON, LED_DISP_OFF };

static const LedSignal::Pattern LED_PATTERNS[] PROGMEM = {
  {500, 500, 1},   // one blink
  {200, 1,   1},   // pulse
  {200, 100, 2},   // two blinks = auto off
  {600, 1,   1},   // long = always on
};
static const uint8_t LED_PATTERN_COUNT = sizeof(LED_PATTERNS) / sizeof(LED_PATTERNS[0]);

// blip on its own pin, else on the status LED
#ifdef GEIGER_BLIPLED
#define LED_FLASH_PIN GEIGER_BLIPLED
#else
#define LED_FLASH_PIN -1
#endif
#ifdef DISABLE_INTERNAL_BLIP
#define LED_FLASH_ON_LED false
#else
#define LED_FLASH_ON_LED true
#endif

#endif
