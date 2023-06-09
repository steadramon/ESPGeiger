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

#define DEFAULT_RX_TIMEOUT           30
#include <Arduino.h>
#ifdef ESPGEIGER_HW
#include "src/ESPGHW/ESPGHW.h"
#endif
#include "src/ConfigManager/ConfigManager.h"
#include "src/Status.h"
#include "src/Counter/Counter.h"
#include "src/Mqtt/MQTT_Client.h"
#include "src/Radmon/Radmon.h"
#include "src/Thingspeak/Thingspeak.h"
#include "src/GMC/GMC.h"
#include "src/Logger/Logger.h"
#include "src/NTP/NTP.h"
#include "src/ArduinoOTA/ArduinoOTA.h"
#include <FS.h>
#include <LittleFS.h>
#include "ArduinoJson.h"
#include <string.h>
#include "AsyncHTTPRequest_Generic.h"
#include "src/OLEDDisplay/OLEDDisplay.h"
#include <Ticker.h>  //Ticker Library
#ifdef SERIALOUT
#include "src/SerialOut/SerialOut.h"
#endif

#if defined(SSD1306_DISPLAY)
SSD1306Display display = SSD1306Display(OLED_ADDR, OLED_SDA, OLED_SCL);
#endif
#ifdef ESPGEIGER_HW
ESPGeigerHW hardware = ESPGeigerHW();
#endif
#ifdef SERIALOUT
SerialOut serialout = SerialOut();
#endif
// Global status and cou
Status status;
Counter gcounter = Counter();

ConfigManager& cManager = ConfigManager::getInstance();

MQTT_Client& mqtt = MQTT_Client::getInstance();

Radmon radmon = Radmon();
Thingspeak thingspeak = Thingspeak();
GMC gmc = GMC();
Ticker msTicker;
Ticker fiveSTicker;
Ticker sTicker;
unsigned long timer_led_measures = 0;

void handleSerial()
{
  if(Serial.available())
  {
    // get the first character
    char serialCmd = Serial.read();

    // dump the serial buffer
    while(Serial.available())
    {
      Serial.read();
    }

    // process serial command
    switch(serialCmd) {
      case 'e':
        cManager.resetSettings();
        ESP.restart();
        break;
      case 'b':
        ESP.restart();
        break;
      default:
        Log::console(PSTR("Unknown command: %c"), serialCmd);
        break;
    }
  }
}
void msTickerCB()
{
  status.led.Update();
#ifdef ESPGEIGER_HW
  status.blip_led.Update();
#endif
}

void fiveSTickerCB()
{
  gmc.loop();
  radmon.loop();
  thingspeak.loop();
}

void sTickerCB()
{
#ifdef SERIALOUT
  serialout.loop();
#endif
#ifdef ESPGEIGER_HW
  hardware.loop();
#endif
  if (status.warmup == true) {
    uint8_t uptime = NTP.getUptime() - status.start;
    if (uptime > 10) {
      status.warmup = false;
    }
  }
  if (status.warmup == false) {
    status.cpm_history.push(gcounter.get_cpm());
  }
}

void setup()
{
  Serial.begin(115200);
  delay(100);

#ifdef ESP8266
  if(!LittleFS.begin()){
#elif defined(ESP32)
  if(!LittleFS.begin(true)){
#endif
    Serial.println("LittleFS Mount Failed");
    return;
  }

  const char* hostName = cManager.getHostName();
  Log::console(PSTR("   ___"));
  Log::console(PSTR("   \\_/    Starting up ... %s"), hostName);
  Log::console(PSTR(".--.O.--. Version - %s/%s (%s)"), status.version, status.git_version, cManager.GetChipModel());
  Log::console(PSTR(" \\/   \\/"));

#ifdef ESPGEIGER_HW
  hardware.begin();
#endif

  delay(100);
  msTicker.attach_ms(1, msTickerCB);
#ifdef SSD1306_DISPLAY
  display.begin();
  if (!cManager.getWiFiIsSaved()) {
    display.setupWifi(hostName);
  }
  #endif
  cManager.autoConnect();
  delay(100);
  cManager.startWebPortal();
  arduino_ota_setup(hostName);
  delay(500);
  mqtt.begin();
  gcounter.begin();
#ifdef SERIALOUT
  serialout.begin();
#endif
  setupNTP();
  status.start = NTP.getUptime();
  sTicker.attach(1, sTickerCB);
  fiveSTicker.attach(5, fiveSTickerCB);
  status.led.Off().Update();
}

void loop()
{
  cManager.process();
  mqtt.loop();

  handleSerial();
  gcounter.loop();
#ifdef SSD1306_DISPLAY
  display.loop();
#endif
  ArduinoOTA.handle();
}