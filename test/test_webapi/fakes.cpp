/*
  fakes.cpp - what WebAPI.cpp calls outside its own file.

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

#include "wapi_fakes.h"

#include "Corium/Logger/Logger.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/System/Clock.h"
#include "Corium/System/CrashDump.h"
#include "Corium/System/FastMillis.h"
#include "Corium/System/TickProfile.h"
#include "Corium/Measure/EGMeasure.h"
#include "Counter/Tube.h"
#include "WebAPI/Features.h"
#include <stdlib.h>

FakeWapi wapi = {};
EnvSensor envsensor;

// other ids are a test bug
float egm_f(const char* id) {
  if (strcmp_P("cpm_st", id) == 0) return wapi.cpm;
  if (strcmp_P("usv_st", id) == 0) return wapi.usv;
  abort();
}
bool egm_b(const char* id) {
  if (strcmp_P("warm", id) == 0) return wapi.warm;
  abort();
}
int16_t Wifi::rssi = 0;
volatile uint32_t _fast_ms_counter = 0;
bool Clock::synced = true;
volatile bool Clock::resync = false;
uint8_t send_indicator = 0;

namespace TickProfile {
  uint32_t lps, tick_max_us;
}

const char EG_K_LAT[]  = "lat";
const char EG_K_LON[]  = "lon";
const char EG_K_MODE[] = "mode";

const char* DeviceInfo::chipid()      { return "c0ffee"; }
const char* DeviceInfo::chipmodel()   { return "ESP8266"; }
const char* DeviceInfo::useragent()   { return "ESPGeiger"; }
const char* Tube::model()             { return "SBM-20"; }
uint8_t  Tube::detection()            { return 6; }
uint16_t Features::flags()            { return 0x0103; }
uint8_t  DeviceInfo::resetReason()    { return wapi.reset_reason; }
bool DeviceInfo::resetExc(uint32_t* epc1, uint32_t* excvaddr, uint8_t* exccause,
                          uint32_t* epc2, uint32_t* epc3, uint32_t* depc) {
  if (!wapi.exc) return false;
  if (epc1) *epc1 = wapi.epc1;
  if (excvaddr) *excvaddr = wapi.excvaddr;
  if (exccause) *exccause = (uint8_t)wapi.exccause;
  if (epc2) *epc2 = wapi.epc2;
  if (epc3) *epc3 = wapi.epc3;
  if (depc) *depc = wapi.depc;
  return true;
}
unsigned long DeviceInfo::uptime()     { return wapi.uptime; }
uint32_t DeviceInfo::freeHeap()        { return wapi.heap; }
uint32_t DeviceInfo::largestFreeBlockLow() { return 0; }
void     DeviceInfo::largestFreeBlockLowReset() {}

const CrashDump::Snapshot* CrashDump::lastCrash() { return nullptr; }
bool CrashDump::hasCrash() { return wapi.snap; }

void WebPortal::sendPageHead(EGHttpResponse&, const __FlashStringHelper*, const char*) {}
void WebPortal::sendPageTail(EGHttpResponse&) {}
void WebPortal::requestRestart(uint32_t) {}
void WebPortal::sendRestartCountdown(EGHttpResponse&) {}

void optimistic_yield(uint32_t) {}

void Log::console(const char*, ...) {}
void Log::debug(const char*, ...) {}

bool EGModuleRegistry::add(EGModule*) { return true; }
bool EGModuleRegistry::set_loop_interval(EGModule*, int32_t ms) {
  wapi.interval = ms;
  return true;
}
bool EGModuleRegistry::sleep_until(EGModule*, unsigned long, unsigned long until) {
  wapi.wake = (uint32_t)until;
  return true;
}
uint32_t EGModuleRegistry::initial_offset(const char*, uint32_t) { return wapi.hs_off; }
unsigned long EGModuleRegistry::ping_from_offset(uint32_t, unsigned long now, uint32_t) { return now; }

float EGPrefs::getFloat(const char*, const char* key) {
  return strcmp(key, EG_K_LAT) == 0 ? wapi.lat : wapi.lon;
}
uint32_t EGPrefs::getUInt(const char*, const char* key) {
  return strcmp(key, EG_K_MODE) == 0 ? wapi.mode : 2;
}
const char* EGPrefs::getString(const char*, const char*) { return ""; }
bool EGPrefs::put(const char*, const char*, const char*) { return true; }
bool EGPrefs::commit(bool) { return true; }

// link only
const char* EGHttpRequest::arg(const __FlashStringHelper*) const { return nullptr; }
void EGHttpResponse::send(uint16_t, const __FlashStringHelper*, const char*) {}
void EGHttpResponse::send(uint16_t, const __FlashStringHelper*, const __FlashStringHelper*) {}
void EGHttpResponse::redirect(const __FlashStringHelper*) {}
bool EGHttpResponse::beginChunked(uint16_t, const __FlashStringHelper*) { return true; }
bool EGHttpResponse::sendChunk(const char*, size_t) { return true; }
bool EGHttpResponse::sendChunk(const char*) { return true; }
bool EGHttpResponse::sendChunk(const __FlashStringHelper*) { return true; }
bool EGHttpResponse::sendKV(const __FlashStringHelper*, const char*) { return true; }
bool EGHttpResponse::sendKV(const __FlashStringHelper*, const char*, const __FlashStringHelper*) { return true; }
bool EGHttpResponse::endChunked() { return true; }
bool EGHttpServer::on(const __FlashStringHelper*, EGHttpRequest::Method, Handler, void*) { return true; }
