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
#include "src/GRNG/GRNG.h"
#include "src/Counter/Counter.h"
#include "src/ConfigManager/ConfigManager.h"
#include "src/Status.h"
#include "src/Mqtt/MQTT_Client.h"
#include "src/Radmon/Radmon.h"
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
#ifdef SERIALOUT
#include "src/SerialOut/SerialOut.h"
#endif
#ifdef GEIGER_SDCARD
SDCard sdcard = SDCard::getInstance();
#endif
#ifdef GEIGER_NEOPIXEL
NeoPixel neopixel = NeoPixel::getInstance();
#endif
#ifdef GEIGER_PUSHBUTTON
PushButton pushbutton = PushButton();
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
// Global status and counter
Status status;
Counter gcounter = Counter();
GRNG grng = GRNG();

NTP_Client ntpclient = NTP_Client();

ConfigManager& cManager = ConfigManager::getInstance();
#ifdef MQTTOUT
MQTT_Client& mqtt = MQTT_Client::getInstance();
#endif
// External
#ifdef GMCOUT
GMC gmc = GMC();
#endif
#ifdef RADMONOUT
Radmon radmon = Radmon();
#endif
#ifdef THINGSPEAKOUT
Thingspeak thingspeak = Thingspeak();
#endif
#ifdef WEBHOOKOUT
Webhook webhook = Webhook();
#endif

SerialCommand serialcmd;     // The demo SerialCommand object

// Tickers
Ticker msTicker;
Ticker hmsTicker;
Ticker fiveSTicker;
Ticker sTicker;

void msTickerCB()
{
  status.led.Update();
#ifdef ESPGEIGER_HW
  status.blip_led.Update();
#endif
}

void hmsTickerCB()
{
  unsigned long now = millis();
#ifdef GEIGER_PUSHBUTTON
  pushbutton.loop(now);
#endif
}

int getQuality() {
  if (WiFi.status() != WL_CONNECTED)
    return -1;
  int dBm = WiFi.RSSI();
  if (dBm <= -100)
    return 0;
  if (dBm >= -50)
    return 100;
  return 2 * (dBm + 100);
}

void sTickerCB()
{
  unsigned long stick_now = millis();

  gcounter.secondticker(stick_now);
#ifdef SERIALOUT
  serialout.loop(stick_now);
#endif
#ifdef ESPGEIGER_HW
  hardware.loop();
#endif
  if (status.warmup) {
    uint8_t uptime = NTP.getUptime() - status.start;
    if (uptime > 10) {
      status.warmup = false;
    }
  }
#ifdef GEIGER_SDCARD
  sdcard.s_tick(stick_now);
#endif
  status.wifi_status = WiFi.status();
  if (status.warmup) {
    return;
  }
  if (status.wifi_disabled) {
    return;
  }
  if (status.wifi_status != WL_CONNECTED) {
    return;
  }
#ifdef MQTTOUT
  mqtt.loop(stick_now);
#endif
#ifdef GMCOUT
  gmc.s_tick(stick_now);
#endif
#ifdef RADMONOUT
  radmon.s_tick(stick_now);
#endif
#ifdef THINGSPEAKOUT
  thingspeak.s_tick(stick_now);
#endif
#ifdef WEBHOOKOUT
  webhook.s_tick(stick_now);
#endif
}

void setup()
{
  Serial.begin(115200);
  Serial.println();
  delay(100);
#ifdef ESP8266
  if(!LittleFS.begin()){
#elif defined(ESP32)
  if(!LittleFS.begin(true)){
#endif
    Serial.println("LittleFS Mount Failed");
    return;
  }
  LittleFS.end();

  const char* hostName = cManager.getHostName();
  Log::console(PSTR("   ___"));
  Log::console(PSTR("   \\_/    Starting up ... %s"), hostName);
  Log::console(PSTR(".--.O.--. Version - %s/%s (%s)"), status.version, status.git_version, cManager.GetChipModel());
  Log::console(PSTR(" \\/   \\/"));
#ifdef GEIGER_NEOPIXEL
  neopixel.setup();
#endif

  delay(100);
  msTicker.attach_ms(1, msTickerCB);
#ifdef SSD1306_DISPLAY
  display.begin();
#endif
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
  pushbutton.begin();
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
    status.wifi_status = WiFi.status();
    cManager.startWebPortal();
  }
#ifdef GEIGER_SDCARD
  sdcard.begin();
#endif
#ifdef ESPGEIGER_HW
  hardware.begin();
#endif

  arduino_ota_setup(hostName);
  delay(500);
  grng.begin();
  if (!status.wifi_disabled) {
#ifdef MQTTOUT
    mqtt.begin();
#endif
    ntpclient.setup();
  }
  gcounter.begin();
  status.start = NTP.getUptime()+1;
#ifdef GEIGER_PUSHBUTTON
  pushbutton.begin();
#endif
  sTicker.attach(1, sTickerCB);
  
  status.led.Off().Update();
  serialcmd.setup();
  hmsTicker.attach_ms(100, hmsTickerCB);
#ifdef SSD1306_DISPLAY
  status.oled_timeout = millis();
#endif
}

void loop()
{
  unsigned long now = millis();
  gcounter.loop(now);
  cManager.process();
#ifndef DISABLE_SERIALRX
  serialcmd.loop();
#endif
#ifdef GEIGER_NEOPIXEL
  // This has to be done in loop
  neopixel.loop(now);
#endif
#ifdef SSD1306_DISPLAY
  // This has to be done in loop
  display.loop(now);
#endif
  ArduinoOTA.handle();
}
