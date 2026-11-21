/*
  ArduinoOTA.cpp - functions to handle Arduino OTA Update

  Copyright (C) 2023 @steadramon

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

#include "ArduinoOTA.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Registry.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/Fs.h"
#include "Corium/Net/IgmpRefresh.h"
#include "Corium/Net/Wifi.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/Output/LedSignal.h"

corium::ArduinoOTAModule ota;
CORIUM_REGISTER_MODULE(ota)

// handle() holds the loop for the whole upload, a real one takes 10-20 s
#define ESPOTA_MAX_MS  120000
#define ESPOTA_IDLE_MS 5000

static uint32_t s_start_ms = 0;
static uint32_t s_moved_ms = 0;
static size_t s_got = 0;
static bool s_stopped = false;
static bool s_ours = false;   // only set by ota_start(), so a refused invite can't end a web /update

static bool ota_too_slow(uint32_t now, size_t got) {
  if (got != s_got) {
    s_got = got;
    s_moved_ms = now;
  }
  return now - s_start_ms >= ESPOTA_MAX_MS || now - s_moved_ms >= ESPOTA_IDLE_MS;
}

// Update's _reset() zeroes the size, which then reads as finished
static void ota_check(size_t got) {
  if (s_stopped || Update.isFinished() || !ota_too_slow(millis(), got)) return;
  s_stopped = true;
  corium::Log::console(PSTR("OTA: Too slow, stopped"));
#ifdef ESP8266
  (void)Update.end();   // image is short, so end() fails and commits nothing
#else
  Update.abort();
#endif
}

static void ota_start() {
  bool fs = ArduinoOTA.getCommand() != U_FLASH;
  s_start_ms = s_moved_ms = millis();
  s_got = 0;
  s_stopped = false;
  s_ours = true;
#ifdef ESP8266
  // writeStream holds the loop for the image, its progress hook runs each pass
  Update.onProgress([](size_t got, size_t) { ota_check(got); });
#endif
  corium::Registry::post(&ota);
#ifndef ESP8266
  // begin() erases nothing, the first write would
  if (corium::otaHeld) {
    s_stopped = true;
    corium::Log::console(PSTR("OTA: table move not done"));
    Update.abort();
  }
#endif
  corium::Lifecycle::send(corium::Lifecycle::OTA_START);
  // Unmount now, else the prefs save on SHUTDOWN goes through the old mount onto the new image
  if (fs) corium::Fs::end();
  corium::LedSignal::lit(true);
  corium::Log::console(PSTR("Start updating %s"), fs ? PSTR("filesystem") : PSTR("sketch"));
}

static void ota_end() {
  corium::Log::console(PSTR("End"));
  corium::LedSignal::lit(false);
  // The core does the restart, not safeRestart()
  corium::Lifecycle::send(corium::Lifecycle::SHUTDOWN);
}

static void ota_progress(unsigned int progress, unsigned int total) {
#ifdef ESP32
  feedLoopWDT();   // handle() blocks for the whole transfer
#endif
#ifndef ESP8266
  ota_check(progress);
#endif
  static uint8_t s_pct = 255;
  if (total == 0) return;
  uint8_t pct = (uint8_t)((uint64_t)progress * 100 / total);
  if (s_pct != pct) {
    corium::Log::debug(PSTR("Progress: %u%%\r"), pct);
    s_pct = pct;
    corium::LedSignal::lit(pct % 2);
  }
}

static void ota_error(ota_error_t error) {
  if (s_ours) {
    s_ours = false;
#ifdef ESP8266
    Update.onProgress(nullptr);   // web /update shares Update
    corium::Wifi::applySleep();   // Update.begin set sleep to none
#endif
    corium::LedSignal::lit(false);
    corium::Lifecycle::send(corium::Lifecycle::OTA_FAILED);
  }
#ifdef ESP8266
  // Refused at Update.begin, which had already set sleep to none
  else if (error == OTA_BEGIN_ERROR && !corium::otaInProgress) corium::Wifi::applySleep();
#endif
  // In ota_error_t order, same on both cores
  static const char ERRS[][8] PROGMEM = {"Auth", "Begin", "Connect", "Receive", "End"};
  if ((unsigned)error <= OTA_END_ERROR) corium::Log::console(PSTR("OTA: %s Failed"), ERRS[error]);
  else                                  corium::Log::console(PSTR("OTA: Error %u"), error);
}

static bool s_espota = false;   // sys.espota at boot

static void espota_begin() {
  // Lambdas come out smaller than plain functions here
  ArduinoOTA.onStart([]() { ota_start(); });
  ArduinoOTA.onEnd([]() { ota_end(); });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) { ota_progress(p, t); });
  ArduinoOTA.onError([](ota_error_t e) { ota_error(e); });

  // The core copies it, and only reads it before begin()
  const char* pass = corium::Prefs::getString("sys", corium::K_WEB_PASS);
  if (pass && pass[0]) ArduinoOTA.setPassword(pass);
  ArduinoOTA.setHostname(corium::DeviceInfo::hostname());
  ArduinoOTA.begin();
}

void corium::ArduinoOTAModule::begin() {
  s_espota = corium::Prefs::getBool("sys", corium::K_ESPOTA);
  if (s_espota) espota_begin();
  else (void)MDNS.begin(corium::DeviceInfo::hostname());   // ArduinoOTA.begin() starts it when espota is on
  // Without mdns the IP still works, so the results are ignored
  (void)MDNS.addService("http",   "tcp", 80);
#ifdef APP_MDNS_SERVICE
  (void)MDNS.addService(APP_MDNS_SERVICE, "tcp", 80);
  const char* fname = corium::DeviceInfo::friendlyName();
  if (fname && fname[0]) {
    MDNS.addServiceTxt(APP_MDNS_SERVICE, "tcp", "fname", fname);
  }
#endif
  corium::Registry::every(this, OTA_POLL_INTERVAL_MS);
}

void corium::ArduinoOTAModule::loop(unsigned long now) {
  if (s_espota) ArduinoOTA.handle();
#ifdef ESP8266
  else MDNS.update();   // handle() does this when espota is on
#endif
  // Multicast groups get re-reported every 5 min
  static unsigned long s_igmp_ms = 0;
  if (s_igmp_ms == 0) s_igmp_ms = now;
  if ((unsigned long)(now - s_igmp_ms) > 300000UL) {
    s_igmp_ms = now;
    corium::IgmpRefresh::refresh();
  }
}
