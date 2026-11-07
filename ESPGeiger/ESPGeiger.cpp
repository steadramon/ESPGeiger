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
#include "Corium/Logger/Logger.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/Boot.h"
#include "Corium/System/BootGuard.h"
#include "Corium/Net/Wifi.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Net/HttpPoster.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/System/Reactor.h"
#include "GRNG/GRNG.h"
#include "Counter/Counter.h"
#include "Counter/CounterMeasures.h"
#include "PulseOut/PulseOut.h"
#include "Corium/WebPortal/WebPortal.h"
#include "App/BootHooks.h"
#include "App/ExternalPause.h"
#include "App/Board.h"
#include "App/LedPatterns.h"
#include "BlipLed/BlipLed.h"
#ifdef ESPGEIGER_HW
#include "HV/HV.h"
#endif

GRNG grng;

// the ms ticker: blip LED and pulse out pins
static inline void ms_poll(uint32_t now_us) {
#ifdef LED_FLASH_ENGINE
  if (BlipLed::due()) BlipLed::poll(now_us);
#endif
#ifdef PULSE_OUT
  if (PulseOut::due()) pulseout.poll(now_us);
#endif
  (void)now_us;   // neither built
}

static void counter_second(uint32_t up) { gcounter.secondticker(up); }

static corium::WebPortal s_portal;

static void boot_banner() {
  corium::Log::banner(PSTR("   ___"));
  corium::Log::banner(PSTR("   \\_/    Starting up ... %s"), corium::DeviceInfo::hostname());
  corium::Log::banner(PSTR(".--.O.--. Version - %s/%s (%s)"), RELEASE_VERSION, GIT_VERSION, BUILD_ENV);
  corium::Log::banner(PSTR(" \\/   \\/"));
  if (corium::BootGuard::crashStreak()) {
    corium::Log::console(PSTR("BootGuard: %u crash boots in a row%s"), corium::BootGuard::crashStreak(),
                 corium::BootGuard::safeMode() ? PSTR(" - safe mode") : PSTR(""));
  }
}

static void boot_button() {
  auto action = BootHooks::checkStartupButtonHold();
  if (action == BootHooks::ButtonHold::FULL_RESET) {
    corium::DeviceInfo::factoryReset();
    corium::DeviceInfo::safeRestart(500);
  } else if (action == BootHooks::ButtonHold::OFFLINE) {
    corium::Wifi::forceOffline();   // no self-heal
  }
}

void setup()
{
#ifdef ESPGEIGER_HW
  // no pull-down on the gate, low till HV::begin (never in safe mode)
  pinMode(GEIGER_PWMPIN, OUTPUT);
  digitalWrite(GEIGER_PWMPIN, LOW);
#endif
  corium::Boot::begin(LED_PATTERNS, LED_PATTERN_COUNT, LED_FLASH_ON_LED);
  Board::claim();
#ifdef APP_BOARD
  corium::DeviceInfo::setBoard(APP_BOARD);
#endif
#ifdef APP_HOME_NAME
  corium::WebPortal::setHomeName(PSTR(APP_HOME_NAME));
#endif
#ifdef LED_FLASH_ENGINE
  BlipLed::begin(LED_FLASH_PIN);
#endif
  measures_begin();
  boot_banner();

  corium::Boot::prefs();
  bool safe = corium::BootGuard::safeMode();
  if (corium::BootGuard::entered()) BootHooks::displaySafeMode();

  delay(100);
  corium::Reactor::clock(corium::Reactor::ms<ms_poll>);
  if (!safe) boot_button();
  Board::detect();
  if (!safe && corium::Wifi::portalAhead()) {
    BootHooks::displaySetupWifi(corium::DeviceInfo::hostname());
  }
  corium::Boot::net(s_portal);

  if (!safe) {
    grng.begin();
    gcounter.begin();
  }
  corium::HttpPoster::hold = ExternalPause::active;
  corium::Boot::end(counter_second);
  BootHooks::displayResetTimeout();
}

// every loop pass, the inline gate first
static inline void counter_pass(unsigned long now)
{
  if (gcounter.needs_loop(now)) gcounter.loop();
}

void loop()
{
  corium::Reactor::run<counter_pass>(s_portal);
}

#ifdef ESP8266
extern "C" void __preloop_update_frequency();
// core sets it every pass, once a second is plenty
extern "C" void preloop_update_frequency() {
  static uint32_t s_next;
  uint32_t now = corium::fastMillis();
  if (!corium::reached(now, s_next)) return;
  s_next = now + 1000;
  __preloop_update_frequency();
}
#endif
