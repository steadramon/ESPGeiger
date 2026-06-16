/***********************************************************************
  ESPGeiger.cpp - Geiger Counter Firmware

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

*************************************************************************/
#if !( defined(ESP8266) ||  defined(ESP32) )
  #error This code is intended to run on the ESP8266 or ESP32 platform! Please check your Tools->Board setting.
#endif

#include <Arduino.h>
#include <LittleFS.h>
#ifdef ESP32
#include "esp_log.h"   // quieten esp_littlefs INFO/WARN chatter on USB serial
#endif
#include <Ticker.h>
// PlatformIO LDF anchor: modules include the .hpp variant only; LDF needs
// the .h here in a project source to discover and link the library.
#include "AsyncHTTPRequest_Generic.h"
#include "src/Logger/Logger.h"
#include "src/Util/DeviceInfo.h"
#include "src/Util/CrashDump.h"
#include "src/Util/BootGuard.h"
#include "src/ImprovSerial/ImprovSerial.h"
#include "src/Util/LedSignal.h"
#include "src/Util/Wifi.h"
#include "src/Util/TickProfile.h"
#include "src/Util/FastMillis.h"
#include "src/Module/EGModuleRegistry.h"
#include "src/ArduinoOTA/ArduinoOTA.h"
#include "src/Prefs/EGPrefs.h"
#include "src/NTP/NTP.h"
#include "src/GRNG/GRNG.h"
#include "src/Counter/Counter.h"
#include "src/WebPortal/WebPortal.h"
#include "src/SerialCommand/SerialCommand.h"
#include "src/Util/BootHooks.h"

long start = 0;

Counter gcounter;
GRNG grng;

SerialCommand serialcmd;

// Tickers
Ticker msTicker;
Ticker sTicker;

bool past_warmup = false;
uint8_t send_indicator = 0;

// Definition for the extern declared in src/Util/FastMillis.h.
volatile uint32_t _fast_ms_counter = 0;
// µs accumulator state for msTickerCB; primed in setup() before attach.
static uint32_t _fast_ms_last_us  = 0;
static uint32_t _fast_ms_accum_us = 0;

void msTickerCB()
{
  // Accumulator catches up if Ticker is starved (no per-call divide).
  uint32_t now_us = micros();
  _fast_ms_accum_us += (now_us - _fast_ms_last_us);
  while (_fast_ms_accum_us >= 1000) {
    _fast_ms_counter++;
    _fast_ms_accum_us -= 1000;
  }
  _fast_ms_last_us = now_us;
  LedSignal::poll();
}

void sTickerCB()
{
  TickProfile::beginTick();
  unsigned long stick_now = fast_millis();
  static bool s_bg_marked = false;
  if (!s_bg_marked && stick_now > 30000 && Wifi::stable_for(5000)) {
    BootGuard::mark_ok();
    s_bg_marked = true;
  }
  gcounter.secondticker();
#ifdef TICK_PROFILE
  TickProfile::markCounter();
#endif

  unsigned long uptime = ntpclient.getUptime() - start;
  if (!past_warmup && uptime > ESPG_WARMUP_S) past_warmup = true;
  Wifi::tick(stick_now);
#ifdef TICK_PROFILE
  TickProfile::markWifi();
#endif

  EGModuleRegistry::tick_all(stick_now, uptime);
#ifdef TICK_PROFILE
  TickProfile::markModules();
#endif

  TickProfile::endTick();
}

static WebPortal s_webPortal;

void setup()
{
  BootGuard::on_boot();
  Serial.begin(115200);
  Serial.println();
  delay(100);
  improvSerial.begin();

#ifdef ESP32
  // Drop INFO/WARN from the FS tags; failed mounts are still detected via begin()==false.
  esp_log_level_set("esp_littlefs", ESP_LOG_ERROR);
  esp_log_level_set("vfs_api",      ESP_LOG_ERROR);
  esp_log_level_set("LITTLEFS",     ESP_LOG_ERROR);
#endif

  CrashDump::begin();
  LedSignal::begin();

  DeviceInfo::begin();

  Log::banner(PSTR("   ___"));
  Log::banner(PSTR("   \\_/    Starting up ... %s"), DeviceInfo::hostname());
  Log::banner(PSTR(".--.O.--. Version - %s/%s (%s)"), RELEASE_VERSION, GIT_VERSION, BUILD_ENV);
  Log::banner(PSTR(" \\/   \\/"));

#ifdef ESP8266
  if(!LittleFS.begin()){
#else
  if(!LittleFS.begin(true)){
#endif
    Log::console(PSTR("LittleFS Mount Failed"));
    return;
  }
  Wifi::check_backup_state();
  LittleFS.end();

  EGModuleRegistry::pre_wifi_all();
  EGPrefs::begin();

  delay(100);
  // Prime fast_millis state to current wall clock so the first Ticker
  // fire sees a real delta, not the whole uptime since boot.
  _fast_ms_last_us = micros();
  _fast_ms_counter = _fast_ms_last_us / 1000;
  msTicker.attach_ms(1, msTickerCB);
  if (start == 0) {
    auto action = BootHooks::checkStartupButtonHold();
    if (action == BootHooks::ButtonHold::FULL_RESET) {
      DeviceInfo::factoryReset();
      DeviceInfo::safeRestart(500);
    } else if (action == BootHooks::ButtonHold::OFFLINE) {
      Wifi::disabled = true;
    }
  }
  if (!Wifi::hasSavedCreds()) {
    BootHooks::displaySetupWifi(DeviceInfo::hostname());
  }
  if (!Wifi::disabled) {
    bool res = Wifi::connectOrPortal();
    if (!res) {
#if (defined(ESPGEIGER_HW) || defined(ESPGEIGER_LT))
      if (!Wifi::hasSavedCreds()) {
        Wifi::disabled = true;
      }
#else
      Log::console(PSTR("WiFi not connecting ... Restarting ... "));
      DeviceInfo::safeRestart(1000);
#endif
    }

    delay(100);
    s_webPortal.begin(80);
  }

  delay(500);
  grng.begin();
  gcounter.begin();
  EGModuleRegistry::begin_all();
  start = ntpclient.getUptime() + 1;
  sTicker.attach(1, sTickerCB);
  
  LedSignal::off();
  BootHooks::displayResetTimeout();
}

void loop()
{
#ifdef LOOP_PROFILE
  uint32_t _body_t0 = micros();
#endif
  TickProfile::countIter();
  unsigned long now = fast_millis();
  if (!ota_in_progress) {
    gcounter.loop();
    s_webPortal.tick(now);
  }
  EGModuleRegistry::loop_all(now);
#ifdef LOOP_PROFILE
  TickProfile::addLoopBody((uint32_t)(micros() - _body_t0));
#endif
}
