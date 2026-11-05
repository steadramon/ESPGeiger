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
#include "Corium/Net/IgmpRefresh.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/Output/LedSignal.h"

corium::ArduinoOTAModule ota;
CORIUM_REGISTER_MODULE(ota)

void corium::ArduinoOTAModule::begin() {
  ArduinoOTA.onStart ([]() {
    const char* type;
    if (ArduinoOTA.getCommand () == U_FLASH)
      type = PSTR("sketch");
    else // U_SPIFFS
      type = PSTR("filesystem");
    corium::Registry::post(&ota);
    corium::Lifecycle::send(corium::Lifecycle::OTA_START);
    corium::LedSignal::lit(true);
    corium::Log::console(PSTR("Start updating %s"), type);
  });
  ArduinoOTA.onEnd ([]() {
    corium::Log::console(PSTR("End"));
    corium::LedSignal::lit(false);
    // core restarts, not safeRestart()
    corium::Lifecycle::send(corium::Lifecycle::SHUTDOWN);
  });
  ArduinoOTA.onProgress ([](unsigned int progress, unsigned int total) {
#ifdef ESP32
    feedLoopWDT();   // handle() blocks for the whole transfer
#endif
    static uint8_t lastValue = 255;
    if (total == 0) return;
    uint8_t nextValue = (uint8_t)((uint64_t)progress * 100 / total);
    if (lastValue != nextValue) {
      corium::Log::debug(PSTR("Progress: %u%%\r"), nextValue);
      lastValue = nextValue;
      corium::LedSignal::lit(nextValue % 2);
    }
  });
  ArduinoOTA.onError ([](ota_error_t error) {
    corium::LedSignal::lit(false);
    if (corium::otaInProgress) corium::Lifecycle::send(corium::Lifecycle::OTA_FAILED);
    // ota_error_t order, both cores
    static const char ERRS[][8] PROGMEM = {"Auth", "Begin", "Connect", "Receive", "End"};
    if ((unsigned)error <= OTA_END_ERROR) corium::Log::console(PSTR("OTA: %s Failed"), ERRS[error]);
    else                                  corium::Log::console(PSTR("OTA: error %u"), error);
  });

  // core copies it, before begin() only, so a new one needs a restart
  const char* pass = corium::Prefs::getString("sys", corium::K_WEB_PASS);
  if (pass && pass[0]) ArduinoOTA.setPassword(pass);
  ArduinoOTA.setHostname(corium::DeviceInfo::hostname());
  ArduinoOTA.begin();
  MDNS.addService("http",   "tcp", 80);
#ifdef APP_MDNS_SERVICE
  // the app's own service, the name it's found by
  MDNS.addService(APP_MDNS_SERVICE, "tcp", 80);
  const char* fname = corium::DeviceInfo::friendlyName();
  if (fname && fname[0]) {
    MDNS.addServiceTxt(APP_MDNS_SERVICE, "tcp", "fname", fname);
  }
#endif
  corium::Registry::every(this, OTA_POLL_INTERVAL_MS);
}

void corium::ArduinoOTAModule::loop(unsigned long now) {
  ArduinoOTA.handle();
  static unsigned long s_last_igmp_refresh = 0;
  if (s_last_igmp_refresh == 0) s_last_igmp_refresh = now;
  if ((unsigned long)(now - s_last_igmp_refresh) > 300000UL) {
    s_last_igmp_refresh = now;
    corium::IgmpRefresh::refresh();
  }
}
