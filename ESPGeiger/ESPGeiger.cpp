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
Counter gcounter = Counter();
GRNG grng = GRNG();

ConfigManager& cManager = ConfigManager::getInstance();

SerialCommand serialcmd;

// Tickers
Ticker msTicker;
Ticker sTicker;

// Loop iteration counter — read & reset every second by sTickerCB
static volatile uint32_t lps_count = 0;

void msTickerCB()
{
  status.led.Update();
#ifdef ESPGEIGER_HW
  status.blip_led.Update();
#endif
}

void sTickerCB()
{
  unsigned long t_start = micros();
  unsigned long stick_now = millis();

  gcounter.secondticker(stick_now);

  unsigned long uptime = NTP.getUptime() - status.start;
  if (status.warmup && uptime > 10) {
    status.warmup = false;
  }
  wl_status_t wifi_status = WiFi.status();

  // WiFi recovery tracking (only once WiFi has been enabled)
  if (!status.wifi_disabled) {
    if (wifi_status != WL_CONNECTED) {
      if (status.wifi_was_connected) {
        status.wifi_was_connected = false;
        status.wifi_lost_at = stick_now;
        Log::console(PSTR("WiFi: Connection lost"));
      }
      unsigned long down_seconds = (stick_now - status.wifi_lost_at) / 1000;
      if (down_seconds > 0 && down_seconds % 30 == 0) {
        Log::console(PSTR("WiFi: Attempting reconnect (%lus)"), down_seconds);
        WiFi.reconnect();
      }
      if (down_seconds > 300) {
        Log::console(PSTR("WiFi: Down for 5 minutes, rebooting"));
        delay(100);
        ESP.restart();
      }
    } else if (!status.wifi_was_connected) {
      status.wifi_was_connected = true;
      if (status.wifi_lost_at > 0) {
        unsigned long down_seconds = (stick_now - status.wifi_lost_at) / 1000;
        Log::console(PSTR("WiFi: Reconnected after %lus"), down_seconds);
      }
    }
  }

  EGModuleRegistry::tick_all(stick_now, uptime);

  status.lps = lps_count;
  lps_count = 0;
  uint32_t this_tick = (uint32_t)(micros() - t_start);
  status.tick_us = (status.tick_us * 7 + this_tick) >> 3;
  if (this_tick > status.tick_max_us) status.tick_max_us = this_tick;
  static uint8_t tick_max_age = 0;
  if (++tick_max_age >= 60) {
    tick_max_age = 0;
    status.tick_max_us = this_tick;
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println();
  delay(100);

  const char* hostName = cManager.getHostName();
  Log::console(PSTR("   ___"));
  Log::console(PSTR("   \\_/    Starting up ... %s"), hostName);
  Log::console(PSTR(".--.O.--. Version - %s/%s (%s)"), status.version, status.git_version, cManager.GetChipModel());
  Log::console(PSTR(" \\/   \\/"));

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
    status.wifi_disabled = true;
  }
#endif
#ifdef SSD1306_DISPLAY
  if (!cManager.getWiFiIsSaved()) {
    display.setupWifi(hostName);
  }
#endif
  if (!status.wifi_disabled) {
    bool res = cManager.autoConnect();
    if (!res) {
#if (defined(ESPGEIGER_HW) || defined(ESPGEIGER_LT))
      if (!cManager.getWiFiIsSaved()) {
        status.wifi_disabled = true;
      }
#else
      Log::console(PSTR("WiFi not connecting ... Restarting ... "));
      delay(1000);
      ESP.restart();
#endif
    }

    delay(100);
    status.wifi_was_connected = (WiFi.status() == WL_CONNECTED);
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
  lps_count++;
  unsigned long now = millis();
  gcounter.loop();
  cManager.processLoop(now);
  EGModuleRegistry::loop_all(now);
}
