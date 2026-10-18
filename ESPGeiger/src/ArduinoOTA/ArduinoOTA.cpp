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
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/Net/IgmpRefresh.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/Output/LedSignal.h"

ArduinoOTAModule ota;
EG_REGISTER_MODULE(ota)

void ArduinoOTAModule::begin() {
  ArduinoOTA.onStart ([]() {
    const char* type;
    if (ArduinoOTA.getCommand () == U_FLASH)
      type = PSTR("sketch");
    else // U_SPIFFS
      type = PSTR("filesystem");
    ota_in_progress = true;
    EGModuleRegistry::wake();
    Lifecycle::send(Lifecycle::OTA_START);
    LedSignal::lit(true);
    Log::console(PSTR("Start updating %s"), type);
  });
  ArduinoOTA.onEnd ([]() {
    Log::console(PSTR("End"));
    LedSignal::lit(false);
    ota_in_progress = false;
    // core restarts, not safeRestart()
    Lifecycle::send(Lifecycle::SHUTDOWN);
  });
  ArduinoOTA.onProgress ([](unsigned int progress, unsigned int total) {
#ifdef ESP32
    feedLoopWDT();   // handle() blocks for the whole transfer
#endif
    static uint8_t lastValue = 255;
    if (total == 0) return;
    uint8_t nextValue = (uint8_t)((uint64_t)progress * 100 / total);
    if (lastValue != nextValue) {
      Log::debug(PSTR("Progress: %u%%\r"), nextValue);
      lastValue = nextValue;
      LedSignal::lit(nextValue % 2);
    }
  });
  ArduinoOTA.onError ([](ota_error_t error) {
    LedSignal::lit(false);
    if (ota_in_progress) Lifecycle::send(Lifecycle::OTA_FAILED);
    ota_in_progress = false;
    if      (error == OTA_AUTH_ERROR)    Log::console(PSTR("OTA: Auth Failed"));
    else if (error == OTA_BEGIN_ERROR)   Log::console(PSTR("OTA: Begin Failed"));
    else if (error == OTA_CONNECT_ERROR) Log::console(PSTR("OTA: Connect Failed"));
    else if (error == OTA_RECEIVE_ERROR) Log::console(PSTR("OTA: Receive Failed"));
    else if (error == OTA_END_ERROR)     Log::console(PSTR("OTA: End Failed"));
    else                                 Log::console(PSTR("OTA: error %u"), error);
  });

  ArduinoOTA.setHostname(DeviceInfo::hostname());
  ArduinoOTA.begin();
  MDNS.addService("http",   "tcp", 80);  // NOLINT(dram)
  MDNS.addService("geiger", "tcp", 80);  // NOLINT(dram)
  const char* fname = DeviceInfo::friendlyName();
  if (fname && fname[0]) {
    MDNS.addServiceTxt("geiger", "tcp", "fname", fname);  // NOLINT(dram)
  }
}

void ArduinoOTAModule::loop(unsigned long now) {
  ArduinoOTA.handle();
  static unsigned long s_last_igmp_refresh = 0;
  if (s_last_igmp_refresh == 0) s_last_igmp_refresh = now;
  if ((unsigned long)(now - s_last_igmp_refresh) > 300000UL) {
    s_last_igmp_refresh = now;
    IgmpRefresh::refresh();
  }
}
