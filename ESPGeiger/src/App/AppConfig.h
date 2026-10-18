/*
  AppConfig.h - What Corium needs to know about this app.

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
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#ifndef THING_NAME
#define THING_NAME "ESPGeiger"
#endif
#define APP_URL     "https://github.com/steadramon/ESPGeiger"
#define APP_LOC_URL "https://loc.espgeiger.com"

#ifdef GEIGER_MODEL
#define APP_MODEL GEIGER_MODEL
#else
#define APP_MODEL ""
#endif

// For builds that only set ESPGEIGER_HW
#ifdef ESPGEIGER_HW
#ifndef GEIGER_BLIPLED
#define GEIGER_BLIPLED 15
#endif
#ifndef ESPG_HV
#define ESPG_HV
#endif
#ifndef ESPG_HV_ADC
#define ESPG_HV_ADC
#endif
#endif

#ifdef GEIGER_BLIPLED
#define BLIP_LED_PIN GEIGER_BLIPLED
#endif

// board name, not the chip
#if defined(ESPGEIGER_HW)
  #define APP_HOME_NAME "ESPGeiger-HW"
  #ifdef ESP8266
    #define APP_BOARD "ESPG-HW"
  #else
    #define APP_BOARD "ESPG32-HW"
  #endif
#elif defined(ESPGEIGER_LT)
  #define APP_HOME_NAME "ESPGeiger-Log"
  #if defined(ESP8266) && defined(GEIGER_SDCARD)
    #define APP_BOARD "ESPG-LOG"
  #elif defined(ESP8266)
    #define APP_BOARD "ESPG-LT"
  #endif
#endif
#ifndef APP_HOME_NAME
#define APP_HOME_NAME THING_NAME
#endif

// saved wifi fails: stay offline, no portal
#if defined(ESPGEIGER_HW) || defined(ESPGEIGER_LT)
#define APP_OFFLINE_OK
#endif

// :00 free for the SD CSV flush
#ifdef GEIGER_SDCARD
#define APP_SEC0_BUSY
#endif

#endif
