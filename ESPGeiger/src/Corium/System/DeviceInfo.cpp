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
#include "Uptime.h"
#include "Lifecycle.h"
#include "../Net/Wifi.h"
#include "../Util/Fnv.h"
#include "Fs.h"

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

static char s_hostname_buf[sizeof(THING_NAME "-") + CHIPID_LEN] = "";
static char s_chipid_buf[CHIPID_LEN + 1] = "";
static char s_useragent_buf[sizeof(THING_NAME "/" RELEASE_VERSION " (" GIT_VERSION "; " BUILD_ENV "; )") + CHIPID_LEN] = "";
static char s_mac_buf[18]        = "";
static char s_flash_buf[24]      = "";
static char s_friendlyname[EG_NAME_MAX + 1] = "";

uint32_t DeviceInfo::freeHeap() {
  static uint32_t cached = 0;
  static uint32_t last_ms = 0;
  uint32_t now = fast_millis();
  if (cached == 0 || now - last_ms >= 1000) {
    uint32_t sample = ESP.getFreeHeap();
    cached = (cached == 0) ? sample : ((cached * 7 + sample) >> 3);
    last_ms = now;
  }
  return cached;
}

static uint32_t s_lfb_cur = 0;
static uint32_t s_lfb_low = 0;  // 0 = unset

uint8_t DeviceInfo::heapFrag() {
  static uint8_t cached = 0;
  static uint32_t last_ms = 0;
  uint32_t now = fast_millis();
  if (last_ms == 0 || now - last_ms >= 10000) {
#ifdef ESP8266
    uint32_t free_;
    uint16_t max_;
    uint8_t frag_;
    ESP.getHeapStats(&free_, &max_, &frag_);
    cached = frag_;
    s_lfb_cur = max_;
#else
    // same as umm_malloc on the 8266
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
    if (s_lfb_low == 0 || s_lfb_cur < s_lfb_low) s_lfb_low = s_lfb_cur;
  }
  return cached;
}

uint32_t DeviceInfo::largestFreeBlock() {
  heapFrag();
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

#ifdef ESP32
static float   s_ct      = NAN;
static uint8_t s_ct_wait = 0;

void DeviceInfo::chipTempSample() {
  if (s_ct_wait) { s_ct_wait--; return; }
  s_ct_wait = 45 + (uint8_t)(esp_random() % 31);
  float t = temperatureRead();
  if (isnan(t) || t <= -40.0f || t >= 150.0f) return;
  s_ct = isnan(s_ct) ? t : s_ct + (t - s_ct) * 0.0625f;
}

float DeviceInfo::chipTempC() { return s_ct; }
#endif

void DeviceInfo::safeRestart(uint32_t delayMs) {
  Lifecycle::send(Lifecycle::SHUTDOWN);
  if (delayMs) delay(delayMs);
  ESP.restart();  // NOLINT(restart-call) the one restart path
}

uint64_t DeviceInfo::hwid() {
#ifdef ESP8266
  return ESP.getChipId();
#else
  return ESP.getEfuseMac();
#endif
}

void DeviceInfo::begin() {
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

  // ESPGeiger-d5536d
  snprintf_P(s_hostname_buf, sizeof(s_hostname_buf), PSTR("%s-%s"),
             THING_NAME, s_chipid_buf);

  snprintf_P(s_useragent_buf, sizeof(s_useragent_buf),
             PSTR("%s/%s (%s; %s; %s)"),
             THING_NAME, RELEASE_VERSION, GIT_VERSION,
             BUILD_ENV, s_chipid_buf);

  const uint8_t fm = (uint8_t)ESP.getFlashChipMode();
  const char* mode = fm == 0 ? PSTR("QIO") : fm == 1 ? PSTR("QOUT") :
                     fm == 2 ? PSTR("DIO") : fm == 3 ? PSTR("DOUT") : PSTR("?");
  const unsigned long mhz = (unsigned long)(ESP.getFlashChipSpeed() / 1000000ul);
#ifdef ESP8266
  snprintf_P(s_flash_buf, sizeof(s_flash_buf), PSTR("%06lx %s %luMHz"),
             (unsigned long)(ESP.getFlashChipId() & 0xFFFFFFul), mode, mhz);
#else
  snprintf_P(s_flash_buf, sizeof(s_flash_buf), PSTR("%s %luMHz"), mode, mhz);
#endif

  // else the MAC can read all FF
#ifdef ESP8266
  WiFi.mode(WIFI_STA);
  WiFi.hostname(s_hostname_buf);
#else
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(s_hostname_buf);
#endif
  strlcpy(s_mac_buf, WiFi.macAddress().c_str(), sizeof(s_mac_buf));
}

const char* DeviceInfo::hostname()    { return s_hostname_buf; }
const char* DeviceInfo::chipid()      { return s_chipid_buf; }
const char* DeviceInfo::useragent()   { return s_useragent_buf; }
const char* DeviceInfo::mac()         { return s_mac_buf; }
const char* DeviceInfo::flashInfo()   { return s_flash_buf; }

void DeviceInfo::factoryReset() {
  // keeps /api.key
  Log::console(PSTR("Factory reset"));
  EGPrefs::reset_all();
  Fs::remove(F("/geigerconfig.json"));   // legacy ConfigManager
  Wifi::clearSavedCreds();
}

const char* DeviceInfo::friendlyName() { return s_friendlyname; }

void DeviceInfo::setFriendlyName(const char* s) {
  strlcpy(s_friendlyname, s ? s : "", sizeof(s_friendlyname));
}

// NOLINTNEXTLINE(complexity) one case per chip
const char* DeviceInfo::chipmodel() {
#ifdef APP_BOARD
  return APP_BOARD;
#elif defined(ESP8266)
  return "ESP8266";
#elif defined(ESP32)
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
#else
  return "UNKNOWN";
#endif
}

bool DeviceInfo::resetExc(uint32_t* epc1, uint32_t* excvaddr, uint8_t* exccause,
                          uint32_t* epc2, uint32_t* epc3, uint32_t* depc) {
#ifdef ESP8266
  rst_info* ri = ESP.getResetInfoPtr();
  if (!ri || ri->reason != REASON_EXCEPTION_RST) return false;
  if (epc1)     *epc1     = ri->epc1;
  if (excvaddr) *excvaddr = ri->excvaddr;
  if (exccause) *exccause = (uint8_t)ri->exccause;
  if (epc2)     *epc2     = ri->epc2;
  if (epc3)     *epc3     = ri->epc3;
  if (depc)     *depc     = ri->depc;
  return true;
#else
  (void)epc1; (void)excvaddr; (void)exccause;
  (void)epc2; (void)epc3; (void)depc;
  return false;
#endif
}

// NOLINTNEXTLINE(complexity) one case per reset cause
uint8_t DeviceInfo::resetReason() {
#ifdef ESP8266
  uint32_t r = ESP.getResetInfoPtr()->reason;
  switch (r) {
    case REASON_DEFAULT_RST:      return RR_POWER_ON;
    case REASON_EXT_SYS_RST:      return RR_PIN;
    case REASON_SOFT_RESTART:     return RR_RESTART;
    case REASON_EXCEPTION_RST:    return RR_EXCEPTION;
    case REASON_WDT_RST:
    case REASON_SOFT_WDT_RST:     return RR_WDT;
    case REASON_DEEP_SLEEP_AWAKE: return RR_DEEP_SLEEP;
    default:                      return RR_UNKNOWN;
  }
#elif defined(ESP32)
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return RR_POWER_ON;
    case ESP_RST_EXT:       return RR_PIN;
    case ESP_RST_SW:        return RR_RESTART;
    case ESP_RST_PANIC:     return RR_EXCEPTION;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return RR_WDT;
    case ESP_RST_BROWNOUT:  return RR_BROWNOUT;
    case ESP_RST_DEEPSLEEP: return RR_DEEP_SLEEP;
    default:                return RR_UNKNOWN;
  }
#else
  return RR_UNKNOWN;
#endif
}

// NOLINTNEXTLINE(complexity) one case per reset cause
const char* DeviceInfo::resetText() {
#ifdef ESP8266
  switch (ESP.getResetInfoPtr()->reason) {
    case REASON_DEFAULT_RST:      return PSTR("Power On");
    case REASON_WDT_RST:          return PSTR("Hardware Watchdog");
    case REASON_EXCEPTION_RST:    return PSTR("Exception");
    case REASON_SOFT_WDT_RST:     return PSTR("Software Watchdog");
    case REASON_SOFT_RESTART:     return PSTR("Software/System restart");
    case REASON_DEEP_SLEEP_AWAKE: return PSTR("Deep-Sleep Wake");
    case REASON_EXT_SYS_RST:      return PSTR("External System");
    default:                      return PSTR("Unknown");
  }
#else
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "Power on";
    case ESP_RST_EXT:       return "External reset";
    case ESP_RST_SW:        return "Software restart";
    case ESP_RST_PANIC:     return "Exception/panic";
    case ESP_RST_INT_WDT:   return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "Task watchdog";
    case ESP_RST_WDT:       return "Watchdog";
    case ESP_RST_BROWNOUT:  return "Brownout";
    case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
    default:                return "Unknown";
  }
#endif
}

uint32_t DeviceInfo::fwFingerprint() {
  uint32_t h = fnv1a(BUILD_ENV, fnv1a(GIT_VERSION));
  if (h == 0) h = 1;   // 0 = not computed
  return h;
}

unsigned long DeviceInfo::uptime() { return Uptime::seconds(); }

char* DeviceInfo::uptimeString() {
  static char buf[20];
  Uptime::Split u = Uptime::split((uint32_t)Uptime::seconds());
  snprintf_P(buf, sizeof(buf), PSTR("%uT%02u:%02u:%02u"), u.d, u.h, u.m, u.s);
  return buf;
}
