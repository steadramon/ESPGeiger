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
#ifdef ESP32
#include "esp_log.h"
#include "esp_task_wdt.h"
#endif
#include <Ticker.h>
#include "Corium/Logger/Logger.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/CrashDump.h"
#include "Corium/System/BootGuard.h"
#include "Corium/System/Fs.h"
#include "Corium/ImprovSerial/ImprovSerial.h"
#include "Corium/Output/LedSignal.h"
#include "Corium/Net/Wifi.h"
#include "Corium/System/TickProfile.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/System/Uptime.h"
#include "GRNG/GRNG.h"
#include "Counter/Counter.h"
#include "PulseOut/PulseOut.h"
#include "Corium/WebPortal/WebPortal.h"
#include "App/BootHooks.h"
#ifdef ESPGEIGER_HW
#include "HV/HV.h"
#endif

long start = 0;

GRNG grng;

Ticker msTicker;
Ticker sTicker;

uint8_t send_indicator = 0;

volatile uint32_t _fast_ms_counter = 0;
static uint32_t _fast_ms_last_us  = 0;
static uint32_t _fast_ms_accum_us = 0;

void msTickerCB()
{
  uint32_t now_us = micros();
  _fast_ms_accum_us += (now_us - _fast_ms_last_us);
  while (_fast_ms_accum_us >= 1000) {
    _fast_ms_counter++;
    _fast_ms_accum_us -= 1000;
  }
  _fast_ms_last_us = now_us;
  if (LedSignal::due()) LedSignal::poll(now_us);
#ifdef PULSE_OUT
  if (PulseOut::due()) pulseout.poll(now_us);
#endif
}

void sTickerCB()
{
  TickProfile::beginTick();
  unsigned long stick_now = fast_millis();
  static bool s_bg_lived = false;
  static bool s_bg_day = false;
  if (!s_bg_day && stick_now > BootGuard::DAY_MS) {
    BootGuard::mark_day();
    s_bg_day = true;
  }
  uint32_t up_raw = (uint32_t)Uptime::tick();
  gcounter.secondticker(up_raw);
#ifdef TICK_PROFILE
  TickProfile::markCounter();
#endif

#ifdef ESP32
  DeviceInfo::chipTempSample();
#endif

  // first tick can land a ms early, no wrap
  unsigned long uptime = (long)up_raw > start ? up_raw - start : 0;
  // wifi off counts as up, else safe mode
  if (!s_bg_lived && uptime >= BootGuard::LIVED_S &&
      (Wifi::stable_for(BootGuard::STABLE_MS) || Wifi::disabled)) {
    BootGuard::mark_lived();
    s_bg_lived = true;
  }
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

#ifdef ESP32
// timer only counts, loop runs the tick like esp8266
static volatile uint8_t s_ticks = 0;
static uint8_t s_ticks_run = 0;
static void sTickerPost() { s_ticks++; }
#endif

static WebPortal s_webPortal;

static void boot_banner() {
  Log::banner(PSTR("   ___"));
  Log::banner(PSTR("   \\_/    Starting up ... %s"), DeviceInfo::hostname());
  Log::banner(PSTR(".--.O.--. Version - %s/%s (%s)"), RELEASE_VERSION, GIT_VERSION, BUILD_ENV);
  Log::banner(PSTR(" \\/   \\/"));
  if (BootGuard::crash_streak()) {
    Log::console(PSTR("BootGuard: %u crash boots in a row%s"), BootGuard::crash_streak(),
                 BootGuard::safe_mode() ? " - safe mode" : "");
  }
}

static void boot_button() {
  auto action = BootHooks::checkStartupButtonHold();
  if (action == BootHooks::ButtonHold::FULL_RESET) {
    DeviceInfo::factoryReset();
    DeviceInfo::safeRestart(500);
  } else if (action == BootHooks::ButtonHold::OFFLINE) {
    Wifi::disabled = true;
    Wifi::offline_forced = true;   // no self-heal
  }
}

static void boot_wifi() {
  if (Wifi::connectOrPortal()) return;
#ifdef APP_OFFLINE_OK
  // tick retries the saved wifi
  Wifi::disabled = true;
#else
  Log::console(PSTR("WiFi not connecting ... Restarting ... "));
  DeviceInfo::safeRestart(1000);
#endif
}

void setup()
{
#ifdef ESPGEIGER_HW
  // no pull-down on the gate, low till HV::begin (never in safe mode)
  pinMode(GEIGER_PWMPIN, OUTPUT);
  digitalWrite(GEIGER_PWMPIN, LOW);
#endif
  CrashDump::begin();
  BootGuard::on_boot();
  Serial.begin(115200);
  Serial.println();
  delay(100);
  improvSerial.begin();

#ifdef ESP32
  esp_log_level_set("esp_littlefs", ESP_LOG_ERROR);
  esp_log_level_set("vfs_api",      ESP_LOG_ERROR);
  esp_log_level_set("LITTLEFS",     ESP_LOG_ERROR);
#endif

#ifdef ESP8266
  analogWriteRange(1023);
#endif
  LedSignal::begin();

  // before anything reads it
  Uptime::tick();
  DeviceInfo::begin();
  boot_banner();

  if (!Fs::begin()) {
    // prefs on defaults, wifi and ota still up
    Log::console(PSTR("LittleFS Mount Failed"));
  }
  Wifi::check_backup_state();

  bool safe = BootGuard::safe_mode();
  EGModuleRegistry::set_safe_mode(safe);
  EGModuleRegistry::pre_wifi_all();
  EGPrefs::begin();
  if (BootGuard::entered()) BootHooks::displaySafeMode();

  delay(100);
  // else first tick sees all of uptime
  _fast_ms_last_us = micros();
  _fast_ms_counter = _fast_ms_last_us / 1000;
  msTicker.attach_ms(1, msTickerCB);
  if (start == 0 && !safe) boot_button();
  if (!Wifi::hasSavedCreds() && !safe) {
    BootHooks::displaySetupWifi(DeviceInfo::hostname());
  }
  if (!Wifi::disabled) {
    boot_wifi();
    delay(100);
    s_webPortal.begin(80);
  }

  delay(500);
  if (!safe) {
    grng.begin();
    gcounter.begin();
  }
  EGModuleRegistry::begin_all();
  LedSignal::attach();
  start = Uptime::tick() + 1;
  if (safe) Lifecycle::request_restart(fast_millis(), BootGuard::retry_after());
#ifdef ESP32
  sTicker.attach(1, sTickerPost);
#else
  sTicker.attach(1, sTickerCB);
#endif

  LedSignal::off();
  BootHooks::displayResetTimeout();
#ifdef ESP32
  // the tick in loop feeds it
  esp_task_wdt_add(NULL);
#endif
}

static inline void pass()
{
#ifdef LOOP_PROFILE
  uint32_t _body_t0 = micros();
#endif
  TickProfile::countIter();
#ifdef ESP32
  // one a pass, a stall catches up
  if (s_ticks != s_ticks_run) {
    s_ticks_run++;
    sTickerCB();
    // once a tick, not each pass
    esp_task_wdt_reset();
  }
#endif
  unsigned long now = fast_millis();
  if (!ota_in_progress) {
    if (gcounter.needs_loop()) gcounter.loop();
    if ((int32_t)(now - s_webPortal.next_tick_due()) >= 0) {
      s_webPortal.tick(now);
    }
  }
  if ((long)(now - EGModuleRegistry::next_loop_due()) >= 0) {
    EGModuleRegistry::loop_all(now);
  }
#ifdef LOOP_PROFILE
  TickProfile::addLoopBody((uint32_t)(micros() - _body_t0));
#endif
}

void loop()
{
#if defined(ESP32) && !CONFIG_FREERTOS_UNICORE
  // never returns
  for (;;) pass();
#else
  pass();
#endif
}
