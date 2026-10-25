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
#ifndef EG_BOOT_H
#define EG_BOOT_H

#include <Arduino.h>
#ifdef ESP32
#include "esp_log.h"
#endif
#include "App/AppConfig.h"
#include "Corium/ImprovSerial/ImprovSerial.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Output/LedSignal.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/System/BootGuard.h"
#include "Corium/System/CrashDump.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/FastMillis.h"
#include "Corium/System/Fs.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/System/Reactor.h"
#include "Corium/System/Uptime.h"
#include "Corium/WebPortal/WebPortal.h"

// begin, prefs, Reactor::clock, net, end; once each, inline into setup()
namespace Boot {

  inline __attribute__((always_inline))
  void begin(const LedSignal::Pattern* leds, uint8_t count, int flash_pin, bool flash_led) {
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
    LedSignal::begin(leds, count, flash_pin, flash_led);

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
    Wifi::check_backup_state();

    EGModuleRegistry::set_safe_mode(BootGuard::safe_mode());
    EGModuleRegistry::pre_wifi_all();
    EGPrefs::begin();
  }

  inline __attribute__((always_inline)) void join() {
    if (Wifi::connectOrPortal()) return;
    if (Wifi::OFFLINE_OK) {
      Wifi::disabled = true;   // tick retries the saved wifi
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
    EGModuleRegistry::begin_all();
    LedSignal::attach();
    if (BootGuard::safe_mode()) Lifecycle::request_restart(fast_millis(), BootGuard::retry_after());
    Reactor::begin(second);
    LedSignal::off();
  }

}

#endif
