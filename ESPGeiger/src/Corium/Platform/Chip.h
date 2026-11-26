/*
  Chip.h - what the chip has, CORIUM_HAS_* always 0 or 1, test with #if

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

#ifndef CORIUM_CHIP_H
#define CORIUM_CHIP_H

#ifdef ESP32
#include <sdkconfig.h>
#include <soc/soc_caps.h>
#endif

// 8266 NMI timer, EGTimer1
#ifdef ESP8266
#define CORIUM_HAS_TIMER1 1
#else
#define CORIUM_HAS_TIMER1 0
#endif

// Only 5 sockets, so pages ship their js in a single request
#ifdef ESP8266
#define CORIUM_HAS_FEW_SOCKETS 1
#else
#define CORIUM_HAS_FEW_SOCKETS 0
#endif

// pwm channels
#ifdef ESP32
#define CORIUM_HAS_LEDC 1
#else
#define CORIUM_HAS_LEDC 0
#endif

// pulse counter, not on the c3
#if defined(ESP32) && SOC_PCNT_SUPPORTED
#define CORIUM_HAS_PCNT 1
#else
#define CORIUM_HAS_PCNT 0
#endif

// rmt, used by the neopixel
#ifdef ESP32
#define CORIUM_HAS_RMT 1
#else
#define CORIUM_HAS_RMT 0
#endif

// die sensor
#ifdef ESP32
#define CORIUM_HAS_CHIPTEMP 1
#else
#define CORIUM_HAS_CHIPTEMP 0
#endif

// analogReadMilliVolts with eFuse calibration. On the 8266 a count is ~1 mV anyway
#ifdef ESP32
#define CORIUM_HAS_ADC_MV 1
#else
#define CORIUM_HAS_ADC_MV 0
#endif

// Hears multicast in modem sleep, the classic esp32 doesn't
#ifdef CONFIG_IDF_TARGET_ESP32
#define CORIUM_HAS_SLEEP_MCAST 0
#else
#define CORIUM_HAS_SLEEP_MCAST 1
#endif

// The classic esp32's core nearly fills its ota slot
#ifdef CONFIG_IDF_TARGET_ESP32
#define CORIUM_HAS_SLOT_ROOM 0
#else
#define CORIUM_HAS_SLOT_ROOM 1
#endif

#endif
