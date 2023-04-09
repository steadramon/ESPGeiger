/***********************************************************************
  Geiger.ino - Geiger Counter Firmware

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
#include "AsyncHTTPRequest_Generic.h"           //https://github.com/khoih-prog/AsyncHTTPRequest_Generic

// Global status and cou
Status status;
Counter gcounter = Counter();

#define INT_PIN    13

ConfigManager& cManager = ConfigManager::getInstance();

MQTT_Client& mqtt = MQTT_Client::getInstance();

Radmon radmon = Radmon();
Thingspeak thingspeak = Thingspeak();
GMC gmc = GMC();

//Ticker geigerTicker;

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

#ifdef ESP8266
  if(!LittleFS.begin()){
#elif defined(ESP32)
  if(!LittleFS.begin(true)){
#endif
    Serial.println("LittleFS Mount Failed");
    return;
  }

  const char* hostName = cManager.getHostName();
  Log::console(PSTR("Starting up ... %s Version - %d/%s (%s)"), hostName, status.version, status.git_version, cManager.GetChipModel());
  delay(100);

  cManager.autoConnect();
  delay(100);
  cManager.startWebPortal();
  arduino_ota_setup(hostName);
  setupNTP();
  mqtt.begin();
  gcounter.begin();
}

void loop()
{
  handleSerial();
  gcounter.loop();
  cManager.process();
  mqtt.loop();
  gmc.loop();
  radmon.loop();
  thingspeak.loop();
  ArduinoOTA.handle();
}
