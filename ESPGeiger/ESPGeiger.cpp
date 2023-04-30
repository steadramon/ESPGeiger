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

#if defined(SSD1306_DISPLAY)
SSD1306Display display(OLED_ADDR, OLED_SDA, OLED_SCL);
#endif

// Global status and cou
Status status;
Counter gcounter = Counter();

ConfigManager& cManager = ConfigManager::getInstance();

MQTT_Client& mqtt = MQTT_Client::getInstance();

Radmon radmon = Radmon();
Thingspeak thingspeak = Thingspeak();
GMC gmc = GMC();

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

void setup()
{
  Serial.begin(115200);
  delay(100);
  pinMode(LED_SEND_RECEIVE, OUTPUT);
  digitalWrite(LED_SEND_RECEIVE, !LED_SEND_RECEIVE_ON);

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
  delay(100);
#ifdef SSD1306_DISPLAY
  display.begin();
#endif
  digitalWrite(LED_SEND_RECEIVE, LED_SEND_RECEIVE_ON);
  cManager.autoConnect();
  digitalWrite(LED_SEND_RECEIVE, !LED_SEND_RECEIVE_ON);
  delay(100);
  cManager.startWebPortal();
  arduino_ota_setup(hostName);
  mqtt.begin();
  gcounter.begin();

  for (int i = 0; i < 6; i++)
  {
    digitalWrite(LED_SEND_RECEIVE, (i%2)?LED_SEND_RECEIVE_ON:!LED_SEND_RECEIVE_ON);
    delay(200);
  }
  setupNTP();
}

void loop()
{
  unsigned long now = millis();

  // Switch off of the LED after TimeLedON
  if (now > (timer_led_measures + (TimeLedON * 1000))) {
    timer_led_measures = millis();
    digitalWrite(LED_SEND_RECEIVE, !LED_SEND_RECEIVE_ON);
  }

  handleSerial();
  gcounter.loop();
#ifdef SSD1306_DISPLAY
  display.loop();
#endif
  cManager.process();
  mqtt.loop();
  gmc.loop();
  radmon.loop();
  thingspeak.loop();
  ArduinoOTA.handle();
}