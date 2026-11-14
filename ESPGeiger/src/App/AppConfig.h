/*
  AppConfig.h - app settings for Corium

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

#include "Corium/Platform/Chip.h"

#ifndef THING_NAME
#define THING_NAME "ESPGeiger"
#endif
#define APP_URL     "https://github.com/steadramon/ESPGeiger"
#define APP_LOC_URL "https://loc.espgeiger.com"
#define APP_LOC_WIN "espgeiger-loc"        // finder window name
#define APP_LOC_MSG "espgeiger-location"   // finder message type
#define APP_MDNS_SERVICE "geiger"          // _geiger._tcp, the gadget looks for it

// /param tabs, corium::PrefCategory order, 6 chars max
#define APP_PARAM_TABS "System", "Input", "Output", "Upload", "Backup"

// favicon and page head, inside a 512 viewBox
#define APP_ICON_SVG \
  "<circle cx='256' cy='256' r='256' fill='#666'/>" \
  "<circle cx='256' cy='256' r='220' fill='#FB2'/>" \
  "<path fill='#555' d='M256 286a30 30 0 1 0 0-60 30 30 0 0 0 0 60zm28-82 62-109a182 182 0 0 0-182 1l63 109a57 57 0 0 1 57-1zm155 51H313c0 21-11 39-28 49l64 108c54-32 90-90 90-157zM163 412l64-108a57 57 0 0 1-28-49H73c0 67 36 125 90 157z'/>"

#ifdef GEIGER_MODEL
#define APP_MODEL GEIGER_MODEL
#else
#define APP_MODEL ""
#endif

// improv fw name: the model if it starts "ESPGeiger-", else "ESPGeiger <model>"
#define APP_FW_NAME (!APP_MODEL[0] ? THING_NAME : \
  strncmp(APP_MODEL, THING_NAME "-", sizeof(THING_NAME)) == 0 ? APP_MODEL : THING_NAME " " APP_MODEL)

// For builds that only set ESPGEIGER_HW, unless they name their LED
#ifdef ESPGEIGER_HW
#if !defined(GEIGER_BLIPLED) && !defined(LED_SEND_RECEIVE)
#define GEIGER_BLIPLED 15
#endif
#ifndef ESPG_HV
#define ESPG_HV
#endif
#ifndef ESPG_HV_ADC
#define ESPG_HV_ADC
#endif
#endif

#if defined(EG_SERIAL_NMI) && defined(ESPG_HV)
#error "EG_SERIAL_NMI and ESPG_HV both need EGTimer1"
#endif

// own pin for the flash, the status LED never flashes counts
#ifdef GEIGER_BLIPLED
#define LED_FLASH_ENGINE
#define CORIUM_LEDSIGNAL_FLASH 0
#endif

// one i2c bus for the OLED and env sensors
#if defined(SSD1306_DISPLAY) || !defined(DISABLE_ENVSENSOR)
#define I2C_BUS
#endif
// a battery clock on the bus: c3/s3, the 8266 and esp32 with -DRTC_CHIP
#if defined(I2C_BUS) && ((defined(ESP32) && CORIUM_HAS_SLOT_ROOM) || defined(RTC_CHIP))
#define CORIUM_RTC
#endif
// the chip's signing key, attest builds
#ifdef ESPGEIGER_ATTEST
#define CORIUM_HWKEY
#endif
#ifdef OLED_PINS_BLOCKED
#define I2C_PINS_FIXED
#endif
#if defined(OLED_SDA)
#define I2C_DEFAULT_SDA OLED_SDA
#define I2C_DEFAULT_SCL OLED_SCL
#elif defined(SSD1306_DISPLAY)
#define I2C_DEFAULT_SDA 4
#define I2C_DEFAULT_SCL 5
#endif

#ifdef GEIGER_BUZZER
#ifndef ESPGEIGER_HW32
#error "GEIGER_BUZZER needs ESPGEIGER_HW32, Board::buzzer() has the coil limits"
#endif
#define BUZZER_ENGINE
#endif

// saved wifi fails: stay offline, no portal
#if defined(ESPGEIGER_HW) || defined(ESPGEIGER_LT)
#define APP_OFFLINE_OK
#endif
// a 401's body
#define APP_AUTH_HELP "Log in as admin with the web password. Forgotten it? " \
  "https://docs.espgeiger.com/install/setup#locked-out-of-the-web-ui"
// another env's bin floats or drives the HV pin
#ifdef ESPG_HV
#define CORIUM_OTA_SAME_ENV
#endif
// Proto or v1, read at boot
#ifdef ESPGEIGER_HW32
#define CORIUM_BOARD_NAME
#endif

// :00 free for the SD CSV flush
#ifdef GEIGER_SDCARD
#define CORIUM_SENDSLOT_FIRST 1
#endif
#ifndef GEIGER_SDCARD_CS
#define GEIGER_SDCARD_CS 16
#endif

// Radmon worst case 376
#define CORIUM_HTTPPOSTER_URL_MAX 384

// old pref groups and where their keys went, first listed wins; display = OLED builds
#define CORIUM_PREFS_MOVED \
  {"display", "i2c"}, \
  {"env",     "i2c"}, \
  {"input",   "counter"}, \
  {"sys",     "counter"}

// 0.12 config file, read once by the legacy import
#define CORIUM_PREFS_LEGACY_FILE "/geigerconfig.json"

// fielded devices hold their key here, never change
#define CORIUM_KEYSTORE_NS    "espg"
#define CORIUM_KEYSTORE_MAGIC 0x4B475345   // "ESGK"

// export blobs start "ESPG", old exports must import
#define CORIUM_EXPORT_MAGIC     0x11, 0x23, 0xC6
#define CORIUM_EXPORT_MAGIC_B64 "ESPG"
#define CORIUM_EXPORT_SEED      0xB11C5EEDu

#endif
