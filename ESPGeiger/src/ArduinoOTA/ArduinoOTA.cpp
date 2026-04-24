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
#include "../Logger/Logger.h"
#include "../Util/Globals.h"
#include "../Module/EGModuleRegistry.h"
#include "../Util/DeviceInfo.h"
#include "../Counter/Counter.h"
#ifdef SSD1306_DISPLAY
#include "../OLEDDisplay/OLEDDisplay.h"
#endif

extern Counter gcounter;

ArduinoOTAModule arduinoOTA;
EG_REGISTER_MODULE(arduinoOTA)

volatile bool ota_in_progress = false;

void ArduinoOTAModule::begin() {
  ArduinoOTA.onStart ([]() {
    const char* type;
    if (ArduinoOTA.getCommand () == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";
#ifdef SSD1306_DISPLAY
    display.showOTABanner();
#endif
    ota_in_progress = true;
    EGModuleRegistry::wake();
    gcounter.stop_for_ota();
    digitalWrite(LED_SEND_RECEIVE, LED_SEND_RECEIVE_ON);
    Log::console(PSTR("Start updating %s"), type);
  });
  ArduinoOTA.onEnd ([]() {
    Log::console(PSTR("End"));
    digitalWrite(LED_SEND_RECEIVE, !LED_SEND_RECEIVE_ON);
    ota_in_progress = false;
  });
  ArduinoOTA.onProgress ([](unsigned int progress, unsigned int total) {
    static uint8_t lastValue = 255;
    uint8_t nextValue = progress / (total / 100);
    if (lastValue != nextValue) {
      Log::debug(PSTR("Progress: %u%%\r"), nextValue);
      lastValue = nextValue;
      digitalWrite(LED_SEND_RECEIVE, (nextValue % 2));
    }
  });
  ArduinoOTA.onError ([](ota_error_t error) {
    Log::debug(PSTR("Error[%u]: %u"), error);
    digitalWrite(LED_SEND_RECEIVE, !LED_SEND_RECEIVE_ON);
    ota_in_progress = false;
    if (error == OTA_AUTH_ERROR) Log::debug(PSTR("Auth Failed"));
    else if (error == OTA_BEGIN_ERROR) Log::debug(PSTR("Begin Failed"));
    else if (error == OTA_CONNECT_ERROR) Log::debug(PSTR("Connect Failed"));
    else if (error == OTA_RECEIVE_ERROR) Log::debug(PSTR("Receive Failed"));
    else if (error == OTA_END_ERROR) Log::debug(PSTR("End Failed"));
  });

  ArduinoOTA.setHostname(DeviceInfo::hostname());
  ArduinoOTA.begin();
  MDNS.addService(K_HTTP, K_TCP, 80);
  MDNS.addService(K_GEIGER, K_TCP, 80);
}

void ArduinoOTAModule::loop(unsigned long now) {
  ArduinoOTA.handle();
}
