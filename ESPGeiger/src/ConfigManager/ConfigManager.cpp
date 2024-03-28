/*
  ConfigManager.cpp - Config Manager class

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
#include "ConfigManager.h"
#include "../Logger/Logger.h"
#include "../Mqtt/MQTT_Client.h"
#include "ArduinoJson.h"
#include <FS.h>
#include <LittleFS.h>
#include "../NTP/timezones.h"

WiFiManagerParameter ESPGeigerParams[] = 
{
  // The broker parameters
  WiFiManagerParameter("<h1>ESPGeiger Settings</h1>"),
  WiFiManagerParameter("geigerModel", "Model", GEIGER_MODEL, 32),
  WiFiManagerParameter("geigerRatio", "Ratio for calculating uSv", "151.0", 10),
#ifndef RXPIN_BLOCKED
  WiFiManagerParameter("geigerRX", "RX Pin", String(GEIGER_RXPIN).c_str(), 12),
#endif
#if GEIGER_TXPIN != -1
#ifndef TXPIN_BLOCKED
  WiFiManagerParameter("geigerTX", "TX Pin", String(GEIGER_TXPIN).c_str(), 12),
#endif
#endif
#if defined(SSD1306_DISPLAY) && defined(GEIGER_PUSHBUTTON)
  WiFiManagerParameter("dispTimeout", "Display timeout (s)", "120", 10),
#endif
#ifdef GEIGER_NEOPIXEL
  WiFiManagerParameter("neopixelBrightness", "NeoPixel Brightness", "10", 10),
#endif
  WiFiManagerParameter("<style>h3{margin-bottom:0;}</style><script>function getE(e){return document.getElementById(e)};function doCB(a,b){getE(a).checked='Y'==getE(b).value;}</script>")
};
#ifdef THINGSPEAKOUT
WiFiManagerParameter TSParams[] = 
{
  // Thingspeak parameters
  WiFiManagerParameter("<br><br><hr><h3>Thingspeak parameters</h3>"),
  WiFiManagerParameter("tsSend", "", "Y", 2, "type='hidden'"),
  WiFiManagerParameter("<input type='checkbox' id='cbts' onchange='getE(\"tsSend\").value = this.checked ? \"Y\":\"N\"'> <label for='cbts'>Send</label>"),
  WiFiManagerParameter("tsChannelKey", "<br>Channel Key", "", 16),
  WiFiManagerParameter(R"J(<script>doCB("cbts","tsSend")</script>)J"),

};
#endif
#ifdef MQTTOUT
WiFiManagerParameter MQTTParams[] = 
{
  // The broker parameters
  WiFiManagerParameter("<br><br><hr><h3>MQTT server</h3>"),
  WiFiManagerParameter("mqttServer", "<br>IP", "", 16, "input='number' pattern='\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}'"),
  WiFiManagerParameter("mqttPort", "Port", "1883", 6, "pattern='\\d{1,5}'"),
  WiFiManagerParameter("mqttUser", "User", "", 255),
  WiFiManagerParameter("mqttPassword", "Password", "", 255, "type='password'"),
  WiFiManagerParameter("mqttTopic", "Root Topic", "ESPGeiger-{id}", 16),
  WiFiManagerParameter("<small>{id} is replaced with the chip ID</small>"),

};
#ifdef MQTTAUTODISCOVER
WiFiManagerParameter HassioParams[] =
{
  // The broker parameters
  WiFiManagerParameter("<br><br><hr><h3>HA Autodiscovery</h3>"),
  WiFiManagerParameter("hassSend", "", MQTT_DISCOVERY, 2, "type='hidden'"),
  WiFiManagerParameter("<input type='checkbox' id='cbhas' onchange='getE(\"hassSend\").value = this.checked ? \"Y\":\"N\"'> <label for='cbhas'>Send</label>"),
  WiFiManagerParameter("hassDisc", "<br>Discovery Topic", S_MQTT_DISCOVERY_TOPIC, 32),
  WiFiManagerParameter(R"J(<script>doCB("cbhas","hassSend")</script>)J"),
};
#endif
#endif
WiFiManagerParameter CloudAPI[] = 
{
  WiFiManagerParameter("<br><br><hr><h3>CloudAPI</h3>"),
  WiFiManagerParameter("apiID", "IP", "", 16, "pattern='\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}'"),
  WiFiManagerParameter("apiSecret", "Port", "1883", 6, "pattern='\\d{1,5}'"),
};
#ifdef RADMONOUT
WiFiManagerParameter radmonParams[] = 
{
  WiFiManagerParameter("<br><br><hr><h3>Radmon.org parameters</h3>"),
  WiFiManagerParameter("radmonSend", "", "Y", 2, "type='hidden'"),
  WiFiManagerParameter("<input type='checkbox' id='cbrm' onchange='getE(\"radmonSend\").value = this.checked ? \"Y\":\"N\"'> <label for='cbrm'>Send</label>"),
  WiFiManagerParameter("radmonUser", "<br>Radmon Username", "", 100),
  WiFiManagerParameter("radmonKey", "Radmon Data PW", "", 100, "type='password'"),
  WiFiManagerParameter(R"J(<script>doCB("cbrm","radmonSend")</script>)J"),
};
#endif
#ifdef GMCOUT
WiFiManagerParameter GMCParams[] = 
{
  WiFiManagerParameter("<br/><br/><hr><h3>GMC parameters</h3>"),
  WiFiManagerParameter("gmcSend", "", "Y", 2, "type='hidden'"),
  WiFiManagerParameter("<input type='checkbox' id='cbgm' onchange='getE(\"gmcSend\").value = this.checked ? \"Y\":\"N\"'> <label for='cbgm'>Send</label>"),
  WiFiManagerParameter("gmcAID", "<br>Account ID", "", 12, "pattern='\\d{1,5}'"),
  WiFiManagerParameter("gmcGCID", "Geiger Counter ID", "", 12, "pattern='\\d{1,12}'"),
  WiFiManagerParameter(R"J(<script>doCB("cbgm","gmcSend")</script>)J"),
};
#endif

ConfigManager::ConfigManager() : WiFiManager(){

  strcat(thingName, status.thingName);

#ifdef ESP8266
  uint32_t tchipId = ESP.getChipId();
#elif defined(ESP32)
  uint32_t tchipId = 0;
  for (int i=0; i<17; i=i+8) {
   tchipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
#endif
  strcat(chipId, String(tchipId, HEX).c_str());

  sprintf_P (hostName, PSTR("%S-%S"), status.thingName, chipId);
  sprintf_P (userAgent, PSTR("%S/%S (%S; %S; %S; %S)"), status.thingName, status.version, status.git_version, status.geiger_model, GetChipModel(), chipId);

  setHostname(hostName);
  setTitle(thingName);
  setCustomHeadElement(faviconHead);
}

void ConfigManager::autoConnect()
{
  WiFiManager::setConnectTimeout(60);
  WiFiManager::autoConnect(hostName);
  Log::console(PSTR("WiFi: IP: %s"), WiFi.localIP().toString().c_str());
}

void ConfigManager::startWebPortal()
{
  for (int i = 0; i < sizeof(ESPGeigerParams) / sizeof(WiFiManagerParameter); i++)
    WiFiManager::addParameter(&ESPGeigerParams[i]);
#ifdef MQTTOUT
  for (int i = 0; i < sizeof(MQTTParams) / sizeof(WiFiManagerParameter); i++)
    WiFiManager::addParameter(&MQTTParams[i]);
#ifdef MQTTAUTODISCOVER
  for (int i = 0; i < sizeof(HassioParams) / sizeof(WiFiManagerParameter); i++)
    WiFiManager::addParameter(&HassioParams[i]);
#endif
#endif
#ifdef RADMONOUT
  for (int i = 0; i < sizeof(radmonParams) / sizeof(WiFiManagerParameter); i++)
    WiFiManager::addParameter(&radmonParams[i]);
#endif
#ifdef THINGSPEAKOUT
  for (int i = 0; i < sizeof(TSParams) / sizeof(WiFiManagerParameter); i++)
    WiFiManager::addParameter(&TSParams[i]);
#endif
#ifdef GMCOUT
  for (int i = 0; i < sizeof(GMCParams) / sizeof(WiFiManagerParameter); i++)
    WiFiManager::addParameter(&GMCParams[i]);
#endif

  ConfigManager::loadParams();

  const char* ratioChar = ConfigManager::getParamValueFromID("geigerRatio");
  float ratio = atof(ratioChar);
  gcounter.set_ratio(ratio);
#if defined(SSD1306_DISPLAY) && defined(GEIGER_PUSHBUTTON)
  int lcdTO = atoi(ConfigManager::getParamValueFromID("dispTimeout"));
  display.setTimeout(lcdTO);
#endif
#ifdef GEIGER_NEOPIXEL
  int npB = atoi(ConfigManager::getParamValueFromID("neopixelBrightness"));
  neopixel.setBrightness(npB);
#endif
  int pin;
#ifndef RXPIN_BLOCKED
  pin = atoi(ConfigManager::getParamValueFromID("geigerRX"));
  gcounter.set_rx_pin(pin);
#endif
#if GEIGER_TXPIN != -1
#ifndef TXPIN_BLOCKED
  pin = atoi(ConfigManager::getParamValueFromID("geigerTX"));
  gcounter.set_tx_pin(pin);
#endif
#endif

  WiFiManager::setWebServerCallback(std::bind(&ConfigManager::bindServerCallback, this));
  WiFiManager::setSaveParamsCallback(std::bind(&ConfigManager::saveParams, this));
  WiFiManager::startWebPortal();
}

void ConfigManager::handleRoot() {
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->sendHeader(F("Pragma"), F("no-cache"));
  server->sendHeader(F("Expires"), F("-1"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, FPSTR(HTTP_HEAD_CT), "");
  String page = FPSTR(HTTP_HEAD_START);
  page.replace(FPSTR(T_v), hostName);
  server->sendContent(page);
  server->sendContent(FPSTR(HTTP_STYLE));
  server->sendContent(FPSTR(faviconHead));
  server->sendContent(FPSTR(HTTP_HEAD_END));
  page = FPSTR(HTTP_ROOT_MAIN);
#ifdef ESPGEIGER_HW
  page.replace(FPSTR(T_t),"ESPGeiger-HW");
#elif defined(ESPGEIGER_LT)
  page.replace(FPSTR(T_t),"ESPGeiger-Log");
#else
  page.replace(FPSTR(T_t),thingName);
#endif
  page.replace(FPSTR(T_v), String(hostName) + " - " + WiFi.localIP().toString()); // use ip if ap is not active for heading @todo use hostname?
  server->sendContent(page);
  server->sendContent(F("<form action='/status' method='get'><button>Status</button></form><br/>"));
  server->sendContent(F("<form action='/hist' method='get'><button>History</button></form><br/>"));
  server->sendContent(F("<form action='/param' method='get'><button>Config</button></form><br/>"));
#ifdef ESPGEIGER_HW
  server->sendContent(F("<form action='/hv' method='get'><button>HV Config</button></form><br/>"));
#endif
  server->sendContent(F("<form action='/ntp' method='get'><button>NTP Config</button></form><br/>"));
  server->sendContent(FPSTR(HTTP_PORTAL_MENU[2]));
  server->sendContent(FPSTR(HTTP_PORTAL_MENU[8]));
  server->sendContent(F("<form action='/restart' method='get'><button>Reboot</button></form><br/>"));
  page = FPSTR(HTTP_STATUS_ON);
  page.replace(FPSTR(T_i),WiFi.localIP().toString());
  page.replace(FPSTR(T_v),htmlEntities(WiFiManager::getWiFiSSID()));
  server->sendContent(page);
  server->sendContent(HTTP_END);
  server->sendContent("");
  server->client().stop();
}

void ConfigManager::handleJSReturn()
{
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->sendHeader(F("Pragma"), F("no-cache"));
  server->sendHeader(F("Expires"), F("-1"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, FPSTR(HTTP_HEAD_CTJS), "");
  server->sendContent(FPSTR(picographJS));
  server->sendContent(FPSTR(statusJS));
  server->sendContent("");
  server->client().stop();
}

void ConfigManager::handleJsonReturn()
{
  char jsonBuffer[256] = "";

  int total = sizeof(jsonBuffer);
  const char* ratioChar = ConfigManager::getParamValueFromID("geigerRatio");
  sprintf_P (
    jsonBuffer,
    PSTR("{\"u\":\"%s\",\"c\":%s,\"c5\":%s,\"c15\":%s,\"cs\":%s,\"r\":%s,\"tc\":%u}"),
    ConfigManager::getUptimeString(),
    String(gcounter.get_cpmf()).c_str(),
    String(gcounter.get_cpm5f()).c_str(),
    String(gcounter.get_cpm15f()).c_str(),
    String(gcounter.get_cps()).c_str(),
    ratioChar,
    gcounter.total_clicks
  );
  jsonBuffer[sizeof(jsonBuffer)-1] = '\0';
  ConfigManager::server.get()->send ( 200, FPSTR(HTTP_HEAD_CTJSON), jsonBuffer );
}

void ConfigManager::handleClicksReturn()
{
  DynamicJsonDocument json(512);

  json.clear();
  auto last_day = json.createNestedArray("last_day");
#ifdef GEIGERTESTMODE
  //json["roll"] = 60;
#endif
  last_day.add(gcounter.clicks_hour);
  int histSize = gcounter.day_hourly_history.size();
  for (decltype(gcounter.day_hourly_history)::index_t i = histSize; i > 0; i--) {
    int value = gcounter.day_hourly_history[i-1];
    last_day.add(value);
  }
  json["today"] = gcounter.clicks_today;
  json["yesterday"] = gcounter.clicks_yesterday;
  json["ratio"] = ConfigManager::getParamValueFromID("geigerRatio");
  if (status.ntp_synced) {
    json["start"] = status.start_time;
  } else {
    unsigned long uptime = NTP.getUptime () - status.start;
    json["uptime"] = uptime;
  }

  char jsonBuffer[512] = "";
  serializeJson(json, jsonBuffer);
  ConfigManager::server.get()->send ( 200, FPSTR(HTTP_HEAD_CTJSON), jsonBuffer );
}


void ConfigManager::handleStatusPage()
{
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->sendHeader(F("Pragma"), F("no-cache"));
  server->sendHeader(F("Expires"), F("-1"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, FPSTR(HTTP_HEAD_CT), "");
  String page = FPSTR(HTTP_HEAD_START);
  String title = FPSTR(hostName);
  title += F(" - Status");
  page.replace(FPSTR(T_v), title);
  server->sendContent(page);
  server->sendContent(FPSTR(HTTP_STYLE));
  server->sendContent(FPSTR(faviconHead));
  server->sendContent(FPSTR(HTTP_HEAD_END));
  page = FPSTR(STATUS_PAGE_BODY_HEAD);
  title = FPSTR(thingName);
  title += F(" - Status");
  page.replace(FPSTR(T_v), title);
  page.replace(FPSTR(T_t), hostName);
  server->sendContent(page);
  server->sendContent(FPSTR(STATUS_PAGE_BODY));
  server->sendContent(FPSTR(HTTP_BACKBTN));
  page = FPSTR(STATUS_PAGE_FOOT);
  page.replace(FPSTR(T_1), String(status.version));
  page.replace(FPSTR(T_2), String(status.git_version));
  server->sendContent(page);
  server->sendContent(FPSTR(HTTP_END));
  server->sendContent("");
  server->client().stop();
}

void ConfigManager::handleHistoryPage()
{
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->sendHeader(F("Pragma"), F("no-cache"));
  server->sendHeader(F("Expires"), F("-1"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, FPSTR(HTTP_HEAD_CT), "");
  String page = FPSTR(HTTP_HEAD_START);
  String title = FPSTR(hostName);
  title += F(" - History");
  page.replace(FPSTR(T_v), title);
  server->sendContent(page);
  server->sendContent(FPSTR(HTTP_STYLE));
  server->sendContent(FPSTR(faviconHead));
  server->sendContent(FPSTR(HTTP_HEAD_END));
  page = FPSTR(STATUS_PAGE_BODY_HEAD);
  title = FPSTR(thingName);
  title += F(" - History");
  page.replace(FPSTR(T_v), title);
  page.replace(FPSTR(T_t), hostName);
  server->sendContent(page);

  server->sendContent(HISTORY_PAGE_BODY);
  server->sendContent(HISTORY_PAGE_JS);
  server->sendContent(FPSTR(HTTP_BACKBTN));

  server->sendContent(FPSTR(HTTP_END));
  server->sendContent("");
  server->client().stop();
}

void ConfigManager::handleNTP()
{
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, FPSTR(HTTP_HEAD_CT), "");
  String page = FPSTR(HTTP_HEAD_START);
  String title = FPSTR(thingName);
  title += F(" - NTP");
  page.replace(FPSTR(T_v), title);
  server->sendContent(page);
  server->sendContent(FPSTR(HTTP_STYLE));
  server->sendContent(FPSTR(faviconHead));
  server->sendContent(FPSTR(HTTP_HEAD_END));
  page = FPSTR(NTP_HTML);
  page.replace(T_i, ntpclient.get_server());
  page.replace(T_v, ntpclient.get_tz());

  server->sendContent(page);
  server->sendContent(FPSTR(NTP_TZ_JS));
  server->sendContent(FPSTR(NTP_JS));

  server->sendContent(FPSTR(HTTP_BACKBTN));
  server->sendContent(FPSTR(HTTP_END));
  server->sendContent("");
  server->client().stop();

}

void ConfigManager::handleNTPSet()
{
  char stmp[64];
  String s = server->arg("s");
  strcpy(stmp, s.c_str());
  ntpclient.set_server(stmp);
  s = server->arg("t");
  strcpy(stmp, s.c_str());
  ntpclient.set_tz(stmp);
  ntpclient.saveconfig();
  ntpclient.setup();

  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, FPSTR(HTTP_HEAD_CT), "");
  String page = FPSTR(HTTP_HEAD_START);
  String title = FPSTR(thingName);
  title += F(" - NTP Saved");
  page.replace(FPSTR(T_v), title);
  server->sendContent(page);
  server->sendContent(FPSTR(HTTP_STYLE));
  server->sendContent(FPSTR(faviconHead));
  server->sendContent(FPSTR(HTTP_HEAD_MREFRESH));
  server->sendContent(FPSTR(HTTP_HEAD_END));
  server->sendContent(HTTP_PARAMSAVED);
  server->sendContent(FPSTR(HTTP_BACKBTN));
  server->sendContent(FPSTR(HTTP_END));
  server->sendContent("");
  server->client().stop();
}

#ifdef ESPGEIGER_HW
void ConfigManager::handleHVPage()
{
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->sendHeader(F("Pragma"), F("no-cache"));
  server->sendHeader(F("Expires"), F("-1"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, FPSTR(HTTP_HEAD_CT), "");
  String page = FPSTR(HTTP_HEAD_START);
  String title = FPSTR(thingName);
  title += F("-HW - HV");
  page.replace(FPSTR(T_v), title);
  server->sendContent(page);
  server->sendContent(FPSTR(HTTP_STYLE));
  server->sendContent(FPSTR(faviconHead));
  server->sendContent(FPSTR(HTTP_HEAD_END));

  page = FPSTR(STATUS_PAGE_BODY_HEAD);
  page.replace(FPSTR(T_v), title);
  page.replace(FPSTR(T_t), hostName);
  server->sendContent(page);

  server->sendContent(HV_STATUS_PAGE_BODY);
  server->sendContent(FPSTR(HTTP_BACKBTN));
  server->sendContent(FPSTR(HTTP_END));
  server->sendContent("");
  server->client().stop();
}

void ConfigManager::handleHVSet()
{
  int _d = atoi(server->arg(F("d")).c_str());
  int _f = atoi(server->arg(F("f")).c_str());
  int _r = atoi(server->arg(F("r")).c_str());
  hardware.set_duty(_d);
  hardware.set_freq(_f);
  hardware.set_vd_ratio(_r);
  hardware.saveconfig();
  ConfigManager::server->send(200, FPSTR(HTTP_HEAD_CT), F("OK"));
}

void ConfigManager::handleHVJSReturn()
{
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->sendHeader(F("Pragma"), F("no-cache"));
  server->sendHeader(F("Expires"), F("-1"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, FPSTR(HTTP_HEAD_CTJS), "");
  server->sendContent(FPSTR(picographJS));
  server->sendContent(FPSTR(hvJS));
  server->sendContent("");
  server->client().stop();
}

void ConfigManager::handleHVJsonReturn()
{
  char jsonBuffer[256] = "";

  int total = sizeof(jsonBuffer);
  int volts = status.hvReading.get();
  int freq = hardware.get_freq();
  int duty = hardware.get_duty();
  int ratio = hardware.get_vd_ratio();

  sprintf_P (
    jsonBuffer,
    PSTR("{\"volts\":%d,\"freq\":%d,\"duty\":%d,\"ratio\":%d}"),
    volts,
    freq,
    duty,
    ratio
  );
  jsonBuffer[sizeof(jsonBuffer)-1] = '\0';
  ConfigManager::server.get()->send ( 200, FPSTR(HTTP_HEAD_CTJSON), jsonBuffer );
}
#endif

void ConfigManager::handleRestart()
{
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->sendHeader(F("Pragma"), F("no-cache"));
  server->sendHeader(F("Expires"), F("-1"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, FPSTR(HTTP_HEAD_CT), "");
  String page = FPSTR(HTTP_HEAD_START);
  page.replace(FPSTR(T_v), "Restarting...");
  server->sendContent(page);
  server->sendContent(FPSTR(HTTP_STYLE));
  server->sendContent(FPSTR(faviconHead));
  server->sendContent(FPSTR(HTTP_HEAD_MREFRESH));
  server->sendContent(FPSTR(HTTP_HEAD_END));
  server->sendContent(FPSTR(thingName));
  server->sendContent(F(" is restarting...<br><br>"));
  server->sendContent(FPSTR(HTTP_END));
  server->sendContent("");
  server->client().stop();
  Log::console(PSTR("Config: Restarting ... "));
  delay(1000);
  ESP.restart();
}

void ConfigManager::resetSettings()
{
  Log::console(PSTR("Config: Erasing ... "));
  LittleFS.begin();
  LittleFS.remove("/geigerconfig.json");
  LittleFS.remove("/ntp.json");
  LittleFS.end();
  WiFiManager::resetSettings();
}

void ConfigManager::handleResetParams()
{
  Log::console(PSTR("Config: Erasing ... "));
  LittleFS.begin();
  LittleFS.remove("/geigerconfig.json");
  LittleFS.remove("/ntp.json");
  LittleFS.end();
  handleRestart();
}
int ConfigManager::getIndexFromID(const char* str)
{
  WiFiManagerParameter** customParams = ConfigManager::getParameters();
  for (int i = 0; i < ConfigManager::getParametersCount(); i++)
  {
    if (customParams[i]->getID() == NULL)
      continue;
    if (strcmp(customParams[i]->getID(), str) == 0)
      return i;
  }
  return -1;
}
void ConfigManager::delay(unsigned long m)
{
  unsigned long delayStart = millis();
  while (m > millis() - delayStart)
  {
    this->process();
    // -- Note: 1ms might not be enough to perform a full yield. So
    // 'yield' in 'doLoop' is eventually a good idea.
    delayMicroseconds(1000);
    yield();
  }
}
void ConfigManager::loadParams()
{
  LittleFS.begin();
  Log::console(PSTR("Config: Loading ..."));
  if (LittleFS.exists("/geigerconfig.json"))
  {
    File configFile = LittleFS.open("/geigerconfig.json", "r");
    if (configFile)
    {
      // Process the json data
      DynamicJsonDocument jsonBuffer(2048);
      DeserializationError error = deserializeJson(jsonBuffer, configFile);
      if (!error)
      {
        WiFiManagerParameter** customParams = ConfigManager::getParameters();
        // Should not be too verbose otherwise it triggers the watchdog reset
        JsonObject root = jsonBuffer.as<JsonObject>();
        for (JsonObject::iterator it = root.begin(); it != root.end(); ++it)
        {
          int idx = getIndexFromID(it->key().c_str());
          if (idx != -1)
          {
            // Should not be too verbose otherwise it triggers the watchdog reset
            // Log::console(PSTR("wifi: key \"%s\" with value \"%s\""), it->key().c_str(), it->value().as<char*>());
            customParams[idx]->setValue(it->value().as<char*>(), customParams[idx]->getValueLength());
          }
          else
            Log::console(PSTR("Config: key \"%s\" with value \"%s\" not found"), it->key().c_str(), it->value().as<char*>());
        }
      }
      else
        Log::console(PSTR("Config: failed to load json params"));
      // Close file
      configFile.close();
    }
    else
    {
      Log::console(PSTR("Config: failed to open config file"));
    }
  }
  else
  {
    Log::console(PSTR("Config: No config file, using defaults"));
  }
  LittleFS.end();

}

void ConfigManager::handleRefreshConsole()
{
  uint32_t counter = 0;

  char stmp[8];
  String s = server->arg("c1");
  strcpy(stmp, s.c_str());
  if (strlen(stmp))
  {
    counter = atoi(stmp);
  }
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->sendHeader(F("Pragma"), F("no-cache"));
  server->sendHeader(F("Expires"), F("-1"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, F("text/plain"), "");
  server->sendContent(String((uint8_t)Log::getLogIdx()) + "\n");
  if (counter != Log::getLogIdx())
  {
    if (!counter)
    {
      counter = Log::getLogIdx();
    }
    do
    {
      char *tmp;
      size_t len;
      Log::getLog(counter, &tmp, &len);
      if (len)
      {
        char stemp[len + 1];
        memcpy(stemp, tmp, len);
        stemp[len - 1] = '\n';
        stemp[len] = '\0';
        server->sendContent(stemp);
      }
      counter++;
      counter &= 0xFF;
      if (!counter)
      {
        counter++;
      } // Skip log index 0 as it is not allowed
    } while (counter != Log::getLogIdx());
  }

  server->sendContent("");
  server->client().stop();
}

void ConfigManager::saveParams()
{
  Log::console(PSTR("Config: Saving ..."));
  LittleFS.begin();

  WiFiManagerParameter** customParams = ConfigManager::getParameters();
  // Open the file
  File configFile = LittleFS.open("/geigerconfig.json", "w");
  if (!configFile)
  {
    Log::console(PSTR("Config: Failed to open geigerconfig.json"));
    return;
  }
  configFile.print("{\n");
  bool printComma=false;
  for (int i = 0; i < ConfigManager::getParametersCount(); i++)
  {
    if (customParams[i]->getID() == NULL)
      continue;
    if (strlen(customParams[i]->getID()) == 0)
      continue;
    if (customParams[i]->getValue() == NULL)
      continue;

    if (printComma)
    {
      configFile.print(",\n");
      printComma=false;
    }

    char tmp[256];
    sprintf(tmp,"\"%s\":\"%s\"",customParams[i]->getID(),customParams[i]->getValue());
    configFile.print(tmp);
    //Log::console(PSTR("Config: writing %s"), tmp);
    printComma=true;
  }
  configFile.print("\n}");

  // Close the file
  configFile.close();
  LittleFS.end();
  Log::console(PSTR("Config: Saved"));
  bool restart_required = false;
#ifndef RXPIN_BLOCKED
  int rx_pin = atoi(ConfigManager::getParamValueFromID("geigerRX"));
  if (rx_pin != gcounter.get_rx_pin()) {
    Log::console(PSTR("Config: Geiger RX Pin Changed"));
    restart_required = true;
  }
#endif
#if GEIGER_TXPIN != -1
#ifndef TXPIN_BLOCKED
  int tx_pin = atoi(ConfigManager::getParamValueFromID("geigerTX"));
  if (tx_pin != gcounter.get_tx_pin()) {
    Log::console(PSTR("Config: Geiger TX Pin"));
    restart_required = true;
  }
#endif
#endif

  if (restart_required) {
    handleRestart();
  }

#ifdef MQTTOUT
  MQTT_Client& mqtt = MQTT_Client::getInstance();
#ifdef MQTTAUTODISCOVER
  const char* _send = getParamValueFromID("hassSend");
  if ((_send == NULL) || (strcmp(_send, "N") == 0)) {
    mqtt.removeHASSConfig();
  }
#endif
  mqtt.disconnect();
  mqtt.begin();
#endif
  const char* ratioChar = ConfigManager::getParamValueFromID("geigerRatio");
  float ratio = atof(ratioChar);
  gcounter.set_ratio(ratio);
#if defined(SSD1306_DISPLAY) && defined(GEIGER_PUSHBUTTON)
  int lcdTO = atoi(ConfigManager::getParamValueFromID("dispTimeout"));
  display.setTimeout(lcdTO);
#endif
#ifdef GEIGER_NEOPIXEL
  int npB = atoi(ConfigManager::getParamValueFromID("neopixelBrightness"));
  neopixel.setBrightness(npB);
#endif
}

const char* ConfigManager::getParamValueFromID(const char* str)
{
  WiFiManagerParameter** customParams = ConfigManager::getParameters();
  for (int i = 0; i < ConfigManager::getParametersCount(); i++)
  {
    if (customParams[i]->getID() == NULL)
      continue;
    if (strncmp(customParams[i]->getID(), str, strlen(str)) == 0)
    {
      if (customParams[i]->getValue() == NULL)
        return NULL;
      if (strlen(customParams[i]->getValue()) == 0)
        return NULL;
      return customParams[i]->getValue();
    }
  }
  return NULL;
}

void ConfigManager::bindServerCallback()
{
  ConfigManager::server.get()->on(ROOT_URL, HTTP_GET, std::bind(&ConfigManager::handleRoot, this));
  ConfigManager::server.get()->on(STATUS_URL, HTTP_GET, std::bind(&ConfigManager::handleStatusPage, this));
  ConfigManager::server.get()->on(JSON_URL, HTTP_GET, std::bind(&ConfigManager::handleJsonReturn, this));
  ConfigManager::server.get()->on(JS_URL, HTTP_GET, std::bind(&ConfigManager::handleJSReturn, this));
  ConfigManager::server.get()->on(CONSOLE_URL, HTTP_GET, std::bind(&ConfigManager::handleRefreshConsole, this));
  ConfigManager::server.get()->on(RESTART_URL, HTTP_GET, std::bind(&ConfigManager::handleRestart, this));
  ConfigManager::server.get()->on(RESET_URL, HTTP_GET, std::bind(&ConfigManager::handleResetParams, this));
  ConfigManager::server.get()->on(NTP_URL, HTTP_GET, std::bind(&ConfigManager::handleNTP, this));
  ConfigManager::server.get()->on(NTP_SET_URL, HTTP_POST, std::bind(&ConfigManager::handleNTPSet, this));
  ConfigManager::server.get()->on(CLICKS_JSON, HTTP_GET, std::bind(&ConfigManager::handleClicksReturn, this));
  ConfigManager::server.get()->on(HIST_URL, HTTP_GET, std::bind(&ConfigManager::handleHistoryPage, this));
#ifdef ESPGEIGER_HW
  ConfigManager::server.get()->on(HV_URL, HTTP_GET, std::bind(&ConfigManager::handleHVPage, this));
  ConfigManager::server.get()->on(HV_SET_URL, HTTP_GET, std::bind(&ConfigManager::handleHVSet, this));
  ConfigManager::server.get()->on(HV_JS_URL, HTTP_GET, std::bind(&ConfigManager::handleHVJSReturn, this));
  ConfigManager::server.get()->on(HV_JSON_URL, HTTP_GET, std::bind(&ConfigManager::handleHVJsonReturn, this));
#endif
}

unsigned long ConfigManager::getUptime () {
  time_t uptime = NTP.getUptime ();
  return uptime;
}

char* ConfigManager::getUptimeString () {
    uint16_t days;
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;

    time_t uptime = NTP.getUptime ();

    seconds = uptime % SECS_PER_MIN;
    uptime -= seconds;
    minutes = (uptime % SECS_PER_HOUR) / SECS_PER_MIN;
    uptime -= minutes * SECS_PER_MIN;
    hours = (uptime % SECS_PER_DAY) / SECS_PER_HOUR;
    uptime -= hours * SECS_PER_HOUR;
    days = uptime / SECS_PER_DAY;

    snprintf (strBuffer, sizeof (strBuffer) - 1, "%dT%02d:%02d:%02d", days, hours, minutes, seconds);

    return strBuffer;
}
