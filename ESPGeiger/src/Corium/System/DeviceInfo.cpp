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
#include "Corium/Util/Words.h"
#include "KeyStore.h"
#include "DeviceInfoChip.h"
#include "CrashDump.h"

static char s_hostname[sizeof(THING_NAME "-") + CHIPID_LEN] = "";
static char s_chipid[CHIPID_LEN + 1] = "";
static uint32_t s_chip_hash = 0;
static char s_mac[18]        = "";
static char s_flash[24]      = "";
static char s_friendlyname[CORIUM_NAME_MAX + 1] = "";

// Sampled once a second, smoothed
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
static const uint32_t FRAG_MS = 60000;   // once a minute, the 8266 getHeapStats masks interrupts for ~260 us

uint8_t corium::DeviceInfo::heapFrag() {
  static uint8_t s_frag = 0;
  static uint32_t s_last_ms = 0;
  uint32_t now = corium::fastMillis();
  if (s_last_ms == 0 || now - s_last_ms >= FRAG_MS) {
    s_frag = Chip::heapRead(&s_lfb_cur);
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
  uint32_t id = Chip::id();
  snprintf_P(s_chipid, sizeof(s_chipid), PSTR("%06lx"),
             (unsigned long)(id & 0xFFFFFFul));
  s_chip_hash = fnv1a(s_chipid);

  snprintf_P(s_hostname, sizeof(s_hostname), PSTR(THING_NAME "-%s"), s_chipid);

  Chip::flashText(s_flash, sizeof(s_flash));

  // Set the mode first, else the MAC can read all FF
  corium::Wifi::staMode(s_hostname);
  strlcpy(s_mac, corium::Wifi::macStr().c_str(), sizeof(s_mac));
}

const char* corium::DeviceInfo::hostname()    { return s_hostname; }
const char* corium::DeviceInfo::chipId()      { return s_chipid; }
uint32_t    corium::DeviceInfo::chipHash()    { return s_chip_hash; }
void corium::DeviceInfo::userAgent(char* buf, size_t cap) {
  snprintf_P(buf, cap, PSTR(THING_NAME "/" RELEASE_VERSION " (" GIT_VERSION "; " BUILD_ENV "; %s)"), s_chipid);
}
const char* corium::DeviceInfo::mac()         { return s_mac; }
const char* corium::DeviceInfo::flashInfo()   { return s_flash; }
static const char BUILD_DATE[] PROGMEM = __DATE__ " " __TIME__;
PGM_P corium::DeviceInfo::buildDate()         { return BUILD_DATE; }

void corium::DeviceInfo::factoryReset() {
  // The device key is kept
  corium::Log::console(W_FACTORY_RESET);
  // Clear this first, else a power cut after the wipe would restore the old login
  if (!corium::KeyStore::clear(WEB_PASS_SLOT)) {
    corium::Log::console(PSTR("Factory reset: Web password backup kept"));
  }
  // before the prefs, else a power cut after them would rejoin the old network with no login
  corium::Wifi::clearSavedCreds();
  corium::Prefs::resetAll();
  // Puts the kept rows back on flash now
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

namespace Chip = corium::DeviceInfo::Chip;
using Chip::ResetRow;

static const ResetRow* reset_row() {
  uint32_t c = Chip::resetCause();
  size_t n = 0;
  const ResetRow* rows = Chip::resets(&n);
  for (size_t i = 0; i < n; i++) {
    if (pgm_read_byte(&rows[i].cause) == c) return &rows[i];
  }
  return nullptr;
}

uint8_t corium::DeviceInfo::resetReason() {
  const ResetRow* r = reset_row();
  return r ? pgm_read_byte(&r->rr) : RR_UNKNOWN;
}

uint8_t corium::DeviceInfo::resetKind() {
  const ResetRow* r = reset_row();
  return r ? pgm_read_byte(&r->kind) : corium::CrashDump::K_UNKNOWN;
}

const char* corium::DeviceInfo::resetText() {
  return corium::CrashDump::kindText(corium::CrashDump::reset().kind);
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
