/*
  Boot.h - setup() in order, the app's own steps go between the calls.

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
#ifndef CORIUM_BOOT_H
#define CORIUM_BOOT_H

#include <Arduino.h>
#include "App/AppConfig.h"
#include "Corium/ImprovSerial/ImprovSerial.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Registry.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Output/LedSignal.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/System/BootGuard.h"
#include "Corium/System/CrashDump.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/FastMillis.h"
#include "Corium/System/Fs.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/System/Reactor.h"
#include "Corium/System/Clock.h"
#include "Corium/System/Uptime.h"
#include "Corium/WebPortal/WebPortal.h"

namespace corium {
// begin, prefs, Reactor::clock, net, end; once each, inline into setup(); no clock = nothing ever due
namespace Boot {

  inline __attribute__((always_inline))
  void begin(const LedSignal::Pattern* leds, uint8_t count, bool flash_led) {
    CrashDump::begin();
    BootGuard::onBoot();
    Serial.begin(115200);
    Serial.println();
    delay(100);
    improvSerial.begin();

#ifdef ESP8266
    analogWriteRange(1023);
#endif
    LedSignal::begin(leds, count, flash_led);

    // before anything reads it
    Uptime::tick();
    DeviceInfo::begin();
  }

  // fs, modules, prefs
  inline __attribute__((always_inline)) void prefs() {
    if (!Fs::begin()) {
      // prefs on defaults, wifi and ota still up
      Log::console(PSTR("LittleFS Mount Failed"));
    }
    Wifi::checkBackupState();

#ifdef CORIUM_CRASH_MARK
    uint8_t rr = DeviceInfo::resetReason();
    Registry::crashBoot(rr == DeviceInfo::RR_WDT || rr == DeviceInfo::RR_EXCEPTION);
#endif
    Registry::setSafeMode(BootGuard::safeMode());
    Registry::preWifiAll();
    Prefs::begin();
    Clock::kept();   // after the zone, stamps from here on
  }

  inline __attribute__((always_inline)) void join() {
    if (Wifi::connectOrPortal()) return;
    if (Wifi::OFFLINE_OK || Wifi::offlineForced) {
      Wifi::disabled = true;   // tick retries the saved wifi, unless forced
      return;
    }
    Log::console(PSTR("WiFi not connecting ... Restarting ... "));
    DeviceInfo::safeRestart(1000);
  }

  // wifi off = no portal
  inline __attribute__((always_inline)) void net(WebPortal& portal) {
    if (!Wifi::disabled) {
      join();
      delay(100);
      portal.begin(80);
    }
    delay(500);
  }

  // modules begin, the tick starts
  inline __attribute__((always_inline)) void end(void (*second)(uint32_t up)) {
    Registry::beginAll();
    LedSignal::attach();
    if (BootGuard::safeMode()) Lifecycle::requestRestart(fastMillis(), BootGuard::retryAfterMs());
    Reactor::begin(second);
    Clock::anchor();
    LedSignal::off();
  }

}
}  // namespace corium

#endif
