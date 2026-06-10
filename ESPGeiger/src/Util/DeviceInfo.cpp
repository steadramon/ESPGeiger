/*
  DeviceInfo.cpp - Device identity and utility functions.

  Copyright (C) 2025 @steadramon

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
#include "DeviceInfo.h"
#include "../Prefs/EGPrefs.h"
#include "../Logger/Logger.h"
#include "../Counter/Counter.h"
#include "Wifi.h"
#include <LittleFS.h>
#ifdef GEIGER_SDCARD
#include "../SDCard/SDCard.h"
#endif

extern Counter gcounter;
#ifdef ESP32
#include <math.h>
#include <esp_chip_info.h>
#include <esp_system.h>
#endif
#ifdef ESP8266
extern "C" {
#include <user_interface.h>
}
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#ifndef GEIGER_MODEL
#define GEIGER_MODEL ""
#endif

// Module-owned identity buffers populated by begin(). hostname is
// "<THING_NAME>-<chipid>" e.g. "ESPGeiger-d5536d".
static char s_hostname_buf[24]   = "";
static char s_chipid_buf[8]      = "";
static char s_useragent_buf[80]  = "";
static char s_mac_buf[18]        = "";
static char s_geigermodel[33]    = GEIGER_MODEL;
static char s_friendlyname[33]   = "";

uint32_t DeviceInfo::freeHeap() {
  static uint32_t cached = 0;
  static uint32_t last_ms = 0;
  uint32_t now = millis();
  if (cached == 0 || now - last_ms >= 1000) {
    uint32_t sample = ESP.getFreeHeap();
    cached = (cached == 0) ? sample : ((cached * 7 + sample) >> 3);
    last_ms = now;
  }
  return cached;
}

static uint8_t  s_hf_peak = 0;
static uint32_t s_lfb_cur = 0;  // current largest free block (bytes)
static uint32_t s_lfb_low = 0;  // low-water mark since last reset; 0 = unset

uint8_t DeviceInfo::heapFrag() {
  static uint8_t cached = 0;
  static uint32_t last_ms = 0;
  uint32_t now = millis();
  if (last_ms == 0 || now - last_ms >= 10000) {
#ifdef ESP8266
    uint32_t free_;
    uint16_t max_;
    uint8_t frag_;
    ESP.getHeapStats(&free_, &max_, &frag_);
    cached = frag_;
    s_lfb_cur = max_;
#else
    // Sqrt-weighted to match ESP8266 umm_malloc semantics. ESP32 has an FPU
    // so sqrtf is a single instruction; this runs at 1 Hz, negligible cost.
    uint32_t free_ = ESP.getFreeHeap();
    uint32_t max_  = ESP.getMaxAllocHeap();
    s_lfb_cur = max_;
    if (free_ == 0 || max_ > free_) {
      cached = 0;
    } else {
      float ratio = (float)max_ / (float)free_;
      cached = (uint8_t)(100.0f - sqrtf(ratio) * 100.0f);
    }
#endif
    last_ms = now;
    if (cached > s_hf_peak) s_hf_peak = cached;
    if (s_lfb_low == 0 || s_lfb_cur < s_lfb_low) s_lfb_low = s_lfb_cur;
  }
  return cached;
}

uint8_t DeviceInfo::heapFragPeak() {
  heapFrag();  // refresh cache + peak if stale
  return s_hf_peak;
}

void DeviceInfo::heapFragPeakReset() {
  s_hf_peak = heapFrag();
}

uint32_t DeviceInfo::largestFreeBlock() {
  heapFrag();  // shared 10s cache refresh
  return s_lfb_cur;
}

uint32_t DeviceInfo::largestFreeBlockLow() {
  heapFrag();
  return s_lfb_low;
}

void DeviceInfo::largestFreeBlockLowReset() {
  largestFreeBlock();
  s_lfb_low = s_lfb_cur;
}

void DeviceInfo::safeRestart(uint32_t delayMs) {
  gcounter.stop_for_ota();
#ifdef GEIGER_SDCARD
  sdcard.pauseWriter();
#endif
  if (delayMs) delay(delayMs);
  ESP.restart();
}

void DeviceInfo::begin() {
  // chipid: low 24 bits of native chip identifier, hex-formatted.
#ifdef ESP8266
  uint32_t tchipId = ESP.getChipId();
#else
  uint32_t tchipId = 0;
  for (int i = 0; i < 17; i += 8) {
    tchipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
#endif
  snprintf_P(s_chipid_buf, sizeof(s_chipid_buf), PSTR("%06lx"),
             (unsigned long)(tchipId & 0xFFFFFFul));

  // hostname = "ESPGeiger-d5536d"
  snprintf_P(s_hostname_buf, sizeof(s_hostname_buf), PSTR("%s-%s"),
             THING_NAME, s_chipid_buf);

  snprintf_P(s_useragent_buf, sizeof(s_useragent_buf),
             PSTR("%s/%s (%s; %s; %s)"),
             THING_NAME, RELEASE_VERSION, GIT_VERSION,
             BUILD_ENV, s_chipid_buf);

  // WiFi.mode(WIFI_STA) idempotent + makes MAC + DHCP-hostname both
  // valid even though Wifi::connectOrPortal hasn't run yet. Otherwise
  // WiFi.macAddress() can return all-FF on first call before the SDK
  // has populated its station struct.
#ifdef ESP8266
  WiFi.mode(WIFI_STA);
  WiFi.hostname(s_hostname_buf);
#else
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(s_hostname_buf);
#endif
  strncpy(s_mac_buf, WiFi.macAddress().c_str(), sizeof(s_mac_buf) - 1);
  s_mac_buf[sizeof(s_mac_buf) - 1] = '\0';
}

const char* DeviceInfo::hostname()    { return s_hostname_buf; }
const char* DeviceInfo::chipid()      { return s_chipid_buf; }
const char* DeviceInfo::useragent()   { return s_useragent_buf; }
const char* DeviceInfo::mac()         { return s_mac_buf; }

void DeviceInfo::factoryReset() {
  // Wipes every module's pref JSON (via EGPrefs) + the WiFi auto-revert
  // backup + any legacy ConfigManager-era config + SDK-stored WiFi creds.
  // /api.key is untouched: not in the EGPrefs file set, not in our
  // explicit remove list - survives by exclusion.
  Log::console(PSTR("Factory reset"));
  EGPrefs::reset_all();
  if (LittleFS.begin()) {
    LittleFS.remove("/wifi_backup");
    LittleFS.remove("/geigerconfig.json");      // legacy ConfigManager
    LittleFS.end();
  }
  Wifi::clearSavedCreds();
}
const char* DeviceInfo::geigermodel() { return s_geigermodel; }

void DeviceInfo::setGeigermodel(const char* s) {
  if (s && s[0]) {
    strncpy(s_geigermodel, s, sizeof(s_geigermodel) - 1);
  } else {
    strncpy(s_geigermodel, GEIGER_MODEL, sizeof(s_geigermodel) - 1);
  }
  s_geigermodel[sizeof(s_geigermodel) - 1] = '\0';
}

uint8_t DeviceInfo::tubeDetection() {
  uint8_t f = 0;
  if (EGPrefs::getBool("input", "tube_alpha")) f |= 0x01;
  if (EGPrefs::getBool("input", "tube_beta"))  f |= 0x02;
  if (EGPrefs::getBool("input", "tube_gamma")) f |= 0x04;
  return f;
}

const char* DeviceInfo::friendlyName() { return s_friendlyname; }

void DeviceInfo::setFriendlyName(const char* s) {
  if (s && s[0]) {
    strncpy(s_friendlyname, s, sizeof(s_friendlyname) - 1);
  } else {
    s_friendlyname[0] = '\0';
  }
  s_friendlyname[sizeof(s_friendlyname) - 1] = '\0';
}

const char* DeviceInfo::chipmodel() {
#ifdef ESP8266
  #ifdef ESPGEIGER_HW
    return "ESPG-HW";
  #elif defined(ESPGEIGER_LT)
    #ifdef GEIGER_SDCARD
    return "ESPG-LOG";
    #else
    return "ESPG-LT";
    #endif
  #else
    return "ESP8266";
  #endif
#elif defined(ESP32)
  #ifdef ESPGEIGER_HW
    return "ESPG32-HW";
  #else
    esp_chip_info_t chipInfo;
    esp_chip_info(&chipInfo);
    switch ((int)chipInfo.model) {
      case 0:  return "ESP8266";
      case (int)esp_chip_model_t::CHIP_ESP32:   return "ESP32";
      case (int)esp_chip_model_t::CHIP_ESP32S2: return "ESP32-S2";
      case (int)esp_chip_model_t::CHIP_ESP32S3: return "ESP32-S3";
      case (int)esp_chip_model_t::CHIP_ESP32C3: return "ESP32-C3";
      case 6:  return "ESP32-H4";
      case 12: return "ESP32-C2";
      case 13: return "ESP32-C6";
      case 16: return "ESP32-H2";
      default: return "ESP32";
    }
  #endif
#else
  return "UNKNOWN";
#endif
}

bool DeviceInfo::resetExc(uint32_t* epc1, uint32_t* excvaddr, uint8_t* exccause) {
#ifdef ESP8266
  rst_info* ri = ESP.getResetInfoPtr();
  if (!ri || ri->reason != REASON_EXCEPTION_RST) return false;
  if (epc1)     *epc1     = ri->epc1;
  if (excvaddr) *excvaddr = ri->excvaddr;
  if (exccause) *exccause = (uint8_t)ri->exccause;
  return true;
#else
  (void)epc1; (void)excvaddr; (void)exccause;
  return false;
#endif
}

uint8_t DeviceInfo::resetReason() {
#ifdef ESP8266
  // ESP8266 rst_info::reason values from user_interface.h:
  //   0 = REASON_DEFAULT_RST (power-on)
  //   1 = REASON_WDT_RST (hardware watchdog)
  //   2 = REASON_EXCEPTION_RST
  //   3 = REASON_SOFT_WDT_RST (software watchdog)
  //   4 = REASON_SOFT_RESTART (ESP.restart())
  //   5 = REASON_DEEP_SLEEP_AWAKE
  //   6 = REASON_EXT_SYS_RST (reset pin / button)
  uint32_t r = ESP.getResetInfoPtr()->reason;
  switch (r) {
    case 0: return 1;
    case 6: return 2;
    case 4: return 3;
    case 2: return 4;
    case 1: case 3: return 5;
    case 5: return 7;
    default: return 0;
  }
#elif defined(ESP32)
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return 1;
    case ESP_RST_EXT:       return 2;
    case ESP_RST_SW:        return 3;
    case ESP_RST_PANIC:     return 4;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return 5;
    case ESP_RST_BROWNOUT:  return 6;
    case ESP_RST_DEEPSLEEP: return 7;
    default:                return 0;
  }
#else
  return 0;
#endif
}

uint16_t DeviceInfo::featureFlags() {
  uint16_t f = 0;
#ifdef MQTTOUT
  f |= 1 << 0;
#endif
#ifdef RADMONOUT
  f |= 1 << 1;
#endif
#ifdef THINGSPEAKOUT
  f |= 1 << 2;
#endif
#ifdef WEBHOOKOUT
  f |= 1 << 3;
#endif
#ifdef SERIALOUT
  f |= 1 << 4;
#endif
#ifdef GMCOUT
  f |= 1 << 5;
#endif
#ifdef SSD1306_DISPLAY
  f |= 1 << 6;
#endif
#ifdef GEIGER_PUSHBUTTON
  f |= 1 << 7;
#endif
#ifdef GEIGER_SDCARD
  f |= 1 << 8;
#endif
#ifdef GEIGER_NEOPIXEL
  f |= 1 << 9;
#endif
#ifdef ESPG_HV
  f |= 1 << 10;
#endif
  return f;
}

char* DeviceInfo::uptimeString() {
  static char buf[20];
  time_t uptime = ntpclient.getUptime();
  uint16_t days = uptime / SECS_PER_DAY;
  uptime %= SECS_PER_DAY;
  uint8_t hours = uptime / SECS_PER_HOUR;
  uptime %= SECS_PER_HOUR;
  uint8_t minutes = uptime / SECS_PER_MIN;
  uint8_t seconds = uptime % SECS_PER_MIN;
  snprintf_P(buf, sizeof(buf), PSTR("%dT%02d:%02d:%02d"), days, hours, minutes, seconds);
  return buf;
}
