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
#include "Corium/Prefs/Prefs.h"
#include "Corium/Prefs/SystemPrefs.h"
#include "Corium/Logger/Logger.h"
#include "Uptime.h"
#include "Lifecycle.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Net/WifiSetup.h"
#include "Corium/Util/Fnv.h"
#include "Corium/Util/MathUtil.h"
#include "KeyStore.h"

#ifdef ESP32
#include <math.h>
#include <esp_chip_info.h>
#include <esp_system.h>
#endif
#ifdef ESP8266
extern "C" {
#include <user_interface.h>
}
#endif

static char s_hostname[sizeof(THING_NAME "-") + CHIPID_LEN] = "";
static char s_chipid[CHIPID_LEN + 1] = "";
static uint32_t s_chip_hash = 0;
static char s_mac[18]        = "";
static char s_flash[24]      = "";
static char s_friendlyname[CORIUM_NAME_MAX + 1] = "";

// sampled once a second, smoothed
uint32_t corium::DeviceInfo::freeHeap() {
  static uint32_t s_heap = 0;
  static uint32_t s_last_ms = 0;
  uint32_t now = corium::fastMillis();
  if (s_heap == 0 || now - s_last_ms >= 1000) {
    uint32_t sample = ESP.getFreeHeap();
    s_heap = (s_heap == 0) ? sample : ema8(s_heap, sample);
    s_last_ms = now;
  }
  return s_heap;
}

static uint32_t s_lfb_cur = 0;
static uint32_t s_lfb_low = 0;  // 0 = unset
#ifdef CORIUM_LFB_TAKE
static uint32_t s_lfb_win = 0;  // since the last take
#endif
static const uint32_t FRAG_MS = 60000;   // 8266 getHeapStats masks ints ~260 us

uint8_t corium::DeviceInfo::heapFrag() {
  static uint8_t s_frag = 0;
  static uint32_t s_last_ms = 0;
  uint32_t now = corium::fastMillis();
  if (s_last_ms == 0 || now - s_last_ms >= FRAG_MS) {
#ifdef ESP8266
    uint32_t free_;
    uint16_t max_;
    uint8_t frag_;
    ESP.getHeapStats(&free_, &max_, &frag_);
    s_frag = frag_;
    s_lfb_cur = max_;
#else
    // same as umm_malloc on the 8266
    uint32_t free_ = ESP.getFreeHeap();
    uint32_t max_  = ESP.getMaxAllocHeap();
    s_lfb_cur = max_;
    if (free_ == 0 || max_ > free_) {
      s_frag = 0;
    } else {
      float ratio = (float)max_ / (float)free_;
      s_frag = (uint8_t)(100.0f - sqrtf(ratio) * 100.0f);
    }
#endif
    s_last_ms = now;
    if (s_lfb_low == 0 || s_lfb_cur < s_lfb_low) s_lfb_low = s_lfb_cur;
#ifdef CORIUM_LFB_TAKE
    if (s_lfb_win == 0 || s_lfb_cur < s_lfb_win) s_lfb_win = s_lfb_cur;
#endif
  }
  return s_frag;
}

uint32_t corium::DeviceInfo::largestFreeBlock() {
  heapFrag();
  return s_lfb_cur;
}

uint32_t corium::DeviceInfo::largestFreeBlockLow() {
  heapFrag();
  return s_lfb_low;
}

#ifdef CORIUM_LFB_TAKE
uint32_t corium::DeviceInfo::largestFreeBlockLowTake() {
  heapFrag();
  uint32_t low = s_lfb_win;
  s_lfb_win = s_lfb_cur;
  return low;
}
#endif

void corium::DeviceInfo::safeRestart(uint32_t delay_ms) {
  corium::Lifecycle::send(corium::Lifecycle::SHUTDOWN);
  if (delay_ms) delay(delay_ms);
  ESP.restart();  // the one restart path
}

void corium::DeviceInfo::begin() {
#ifdef ESP8266
  uint32_t id = ESP.getChipId();
#else
  uint32_t id = 0;
  // last 3 MAC bytes, like the 8266 chip id
  for (int i = 0; i < 17; i += 8) {
    id |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
#endif
  snprintf_P(s_chipid, sizeof(s_chipid), PSTR("%06lx"),
             (unsigned long)(id & 0xFFFFFFul));
  s_chip_hash = fnv1a(s_chipid);

  // <THING_NAME>-d5536d
  snprintf_P(s_hostname, sizeof(s_hostname), PSTR("%s-%s"),
             THING_NAME, s_chipid);

  const uint8_t fm = (uint8_t)ESP.getFlashChipMode();
  static const char FLASH_MODES[][5] PROGMEM = {"QIO", "QOUT", "DIO", "DOUT"};
  const char* mode = fm < 4 ? FLASH_MODES[fm] : PSTR("?");
  const unsigned long mhz = (unsigned long)(ESP.getFlashChipSpeed() / 1000000ul);
#ifdef ESP8266
  snprintf_P(s_flash, sizeof(s_flash), PSTR("%06lx %s %luMHz"),
             (unsigned long)(ESP.getFlashChipId() & 0xFFFFFFul), mode, mhz);
#else
  snprintf_P(s_flash, sizeof(s_flash), PSTR("%s %luMHz"), mode, mhz);
#endif

  // else the MAC can read all FF
  corium::Wifi::staMode(s_hostname);
  strlcpy(s_mac, corium::Wifi::macStr().c_str(), sizeof(s_mac));
}

const char* corium::DeviceInfo::hostname()    { return s_hostname; }
const char* corium::DeviceInfo::chipId()      { return s_chipid; }
uint32_t    corium::DeviceInfo::chipHash()    { return s_chip_hash; }
void corium::DeviceInfo::userAgent(char* buf, size_t cap) {
  snprintf_P(buf, cap, PSTR("%s/%s (%s; %s; %s)"),
             THING_NAME, RELEASE_VERSION, GIT_VERSION, BUILD_ENV, s_chipid);
}
const char* corium::DeviceInfo::mac()         { return s_mac; }
const char* corium::DeviceInfo::flashInfo()   { return s_flash; }
static const char BUILD_DATE[] PROGMEM = __DATE__ " " __TIME__;
PGM_P corium::DeviceInfo::buildDate()         { return BUILD_DATE; }

void corium::DeviceInfo::factoryReset() {
  // keeps the key
  corium::Log::console(PSTR("Factory reset"));
  // first, a cut after the wipe would restore the old login
  if (!corium::KeyStore::clear(WEB_PASS_SLOT)) {
    corium::Log::console(PSTR("Factory reset: Web password backup kept"));   // the old login comes back
  }
  // before the prefs, a cut after them would rejoin the old network wide open
  corium::Wifi::clearSavedCreds();
  corium::Prefs::resetAll();
  // kept rows back on flash now
  if (!corium::Prefs::commit(false)) corium::Log::console(PSTR("Factory reset: Not saved"));
}

static const char* s_board = nullptr;
const char* corium::DeviceInfo::board() { return s_board ? s_board : chip(); }
void corium::DeviceInfo::setBoard(const char* s) { s_board = s; }
#ifdef CORIUM_BOARD_NAME
static const char* s_board_name = nullptr;
const char* corium::DeviceInfo::boardName() { return s_board_name; }
void corium::DeviceInfo::setBoardName(const char* s) { s_board_name = s; }
#endif

const char* corium::DeviceInfo::friendlyName() { return s_friendlyname; }

void corium::DeviceInfo::setFriendlyName(const char* s) {
  strlcpy(s_friendlyname, s ? s : "", sizeof(s_friendlyname));
}

// one case per chip
const char* corium::DeviceInfo::chip() {
#ifdef ESP8266
  return "ESP8266";
#elif defined(ESP32)
  esp_chip_info_t info;
  esp_chip_info(&info);
  switch ((int)info.model) {
    case 0:  return "ESP8266";
    case (int)esp_chip_model_t::CHIP_ESP32:   return "ESP32";
    case (int)esp_chip_model_t::CHIP_ESP32S2: return "ESP32-S2";
    case (int)esp_chip_model_t::CHIP_ESP32S3: return "ESP32-S3";
    case (int)esp_chip_model_t::CHIP_ESP32C3: return "ESP32-C3";
    // raw model numbers, not in every IDF enum
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

struct ResetRow {
  uint8_t cause;   // the sdk's
  uint8_t rr;
  char text[19];
};

#ifdef ESP8266
static const ResetRow RESETS[] PROGMEM = {
  {REASON_DEFAULT_RST,      corium::DeviceInfo::RR_POWER_ON,   "Power on"},
  {REASON_WDT_RST,          corium::DeviceInfo::RR_WDT,        "Hardware watchdog"},
  {REASON_EXCEPTION_RST,    corium::DeviceInfo::RR_EXCEPTION,  "Exception"},
  {REASON_SOFT_WDT_RST,     corium::DeviceInfo::RR_WDT,        "Software watchdog"},
  {REASON_SOFT_RESTART,     corium::DeviceInfo::RR_RESTART,    "Software restart"},
  {REASON_DEEP_SLEEP_AWAKE, corium::DeviceInfo::RR_DEEP_SLEEP, "Deep sleep wake"},
  {REASON_EXT_SYS_RST,      corium::DeviceInfo::RR_PIN,        "External reset"},
};
static uint32_t reset_cause() { return ESP.getResetInfoPtr()->reason; }
#else
static const ResetRow RESETS[] = {
  {ESP_RST_POWERON,   corium::DeviceInfo::RR_POWER_ON,   "Power on"},
  {ESP_RST_EXT,       corium::DeviceInfo::RR_PIN,        "External reset"},
  {ESP_RST_SW,        corium::DeviceInfo::RR_RESTART,    "Software restart"},
  {ESP_RST_PANIC,     corium::DeviceInfo::RR_EXCEPTION,  "Exception"},
  {ESP_RST_INT_WDT,   corium::DeviceInfo::RR_WDT,        "Interrupt watchdog"},
  {ESP_RST_TASK_WDT,  corium::DeviceInfo::RR_WDT,        "Task watchdog"},
  {ESP_RST_WDT,       corium::DeviceInfo::RR_WDT,        "Watchdog"},
  {ESP_RST_BROWNOUT,  corium::DeviceInfo::RR_BROWNOUT,   "Brownout"},
  {ESP_RST_DEEPSLEEP, corium::DeviceInfo::RR_DEEP_SLEEP, "Deep sleep wake"},
};
static uint32_t reset_cause() { return esp_reset_reason(); }
#endif

static const ResetRow* reset_row() {
  uint32_t c = reset_cause();
  for (const ResetRow& r : RESETS) {
    if (pgm_read_byte(&r.cause) == c) return &r;
  }
  return nullptr;
}

uint8_t corium::DeviceInfo::resetReason() {
  const ResetRow* r = reset_row();
  return r ? pgm_read_byte(&r->rr) : RR_UNKNOWN;
}

const char* corium::DeviceInfo::resetText() {
  const ResetRow* r = reset_row();
  return r ? r->text : PSTR("Unknown");
}

uint32_t corium::DeviceInfo::fwFingerprint() {
  uint32_t h = fnv1a(BUILD_ENV, fnv1a(GIT_VERSION));
  if (h == 0) h = 1;   // 0 = not computed
  return h;
}

unsigned long corium::DeviceInfo::uptime() { return corium::Uptime::seconds(); }

char* corium::DeviceInfo::uptimeString() {
  static char s_buf[20];
  corium::Uptime::Split u = corium::Uptime::split((uint32_t)corium::Uptime::seconds());
  snprintf_P(s_buf, sizeof(s_buf), PSTR("%ud %02u:%02u:%02u"), u.d, u.h, u.m, u.s);
  return s_buf;
}
