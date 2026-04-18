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
#include "src/Util/DeviceInfo.h"
#include "src/Util/Wifi.h"
#include "src/Util/TickProfile.h"
#ifdef ESPGEIGER_HW
#include "src/ESPGHW/ESPGHW.h"
#endif
#include "src/GRNG/GRNG.h"
#include "src/Counter/Counter.h"
#include "src/ConfigManager/ConfigManager.h"
#include "src/Status.h"
#include "src/Mqtt/MQTT_Client.h"
#include "src/Radmon/Radmon.h"
#include "src/Module/EGModuleRegistry.h"
#include "src/Thingspeak/Thingspeak.h"
#include "src/GMC/GMC.h"
#include "src/Webhook/Webhook.h"
#include "src/Logger/Logger.h"
#include "src/SDCard/SDCard.h"
#include "src/NeoPixel/NeoPixel.h"
#include "src/PushButton/PushButton.h"
#include "src/NTP/NTP.h"
#include "src/ArduinoOTA/ArduinoOTA.h"
#include "src/Prefs/EGPrefs.h"
#include <FS.h>
#include <LittleFS.h>
#include "ArduinoJson.h"
#include <string.h>
#include "AsyncHTTPRequest_Generic.h"
#include "src/OLEDDisplay/OLEDDisplay.h"
#include "src/SerialCommand/SerialCommand.h"
#include <Ticker.h>  //Ticker Library
// Global status and counter
Status status;
Counter gcounter;
GRNG grng;

ConfigManager& cManager = ConfigManager::getInstance();

SerialCommand serialcmd;

// Tickers
Ticker msTicker;
Ticker sTicker;

void msTickerCB()
{
  status.led.Update();
#ifdef ESPGEIGER_HW
  status.blip_led.Update();
#endif
}

void sTickerCB()
{
  TickProfile::beginTick();
  unsigned long stick_now = millis();

  gcounter.secondticker(stick_now);
#ifdef TICK_PROFILE
  TickProfile::markCounter();
#endif

  unsigned long uptime = NTP.getUptime() - status.start;
  if (status.warmup && uptime > 10) {
    status.warmup = false;
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

void setup()
{
  Serial.begin(115200);
  Serial.println();
  delay(100);

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
  LittleFS.end();

  EGModuleRegistry::pre_wifi_all();
  EGPrefs::begin();

  delay(100);
  msTicker.attach_ms(1, msTickerCB);
#ifdef GEIGER_PUSHBUTTON
  pushbutton.init();
  if (pushbutton.isPressed() && status.start == 0) {
#ifdef SSD1306_DISPLAY
    display.wifiDisabled();
#endif
    while (pushbutton.isPressed()) {
      delay(250);
      pushbutton.read();
    }
    delay(750);
    status.enable_oled_timeout = false;
    Wifi::disabled = true;
  }
#endif
#ifdef SSD1306_DISPLAY
  if (!cManager.getWiFiIsSaved()) {
    display.setupWifi(DeviceInfo::hostname());
  }
#endif
  if (!Wifi::disabled) {
    bool res = cManager.autoConnect();
    if (!res) {
#if (defined(ESPGEIGER_HW) || defined(ESPGEIGER_LT))
      if (!cManager.getWiFiIsSaved()) {
        Wifi::disabled = true;
      }
#else
      Log::console(PSTR("WiFi not connecting ... Restarting ... "));
      delay(1000);
      ESP.restart();
#endif
    }

    delay(100);
    cManager.startWebPortal();
  }

  delay(500);
  grng.begin();
  gcounter.begin();
  EGModuleRegistry::begin_all();
  status.start = NTP.getUptime() + 1;
  sTicker.attach(1, sTickerCB);
  
  status.led.Off().Update();
  status.oled_timeout = millis();
}

void loop()
{
  TickProfile::countIter();
  unsigned long now = millis();
  gcounter.loop();
  cManager.processLoop(now);
  EGModuleRegistry::loop_all(now);
}
