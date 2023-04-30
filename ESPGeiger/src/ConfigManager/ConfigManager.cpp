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

WiFiManagerParameter ESPGeigerParams[] = 
{
  // The broker parameters
  WiFiManagerParameter("<h1>ESPGeiger Settings</h1>"),
  WiFiManagerParameter("geigerModel", "Model", GEIGER_MODEL, 32),
  WiFiManagerParameter("geigerRatio", "Ratio for calculating uSv", "151.0", 10),
  WiFiManagerParameter("<style>h3{margin-bottom:0;}</style><script>function getE(e){return document.getElementById(e)}</script>")
};

WiFiManagerParameter TSParams[] = 
{
  // Thingspeak parameters
  WiFiManagerParameter("<br/><br/><hr><h3>Thingspeak parameters</h3>"),
  WiFiManagerParameter("tsSend", "", "Y", 2, "type='hidden'"),
  WiFiManagerParameter("<input type='checkbox' id='cbts' onchange='getE(\"tsSend\").value = this.checked ? \"Y\":\"N\"'> <label for='cbts'>Send</label>"),
  WiFiManagerParameter("tsChannelKey", "<br>Channel Key", "", 16),
};

WiFiManagerParameter MQTTParams[] = 
{
  // The broker parameters
  WiFiManagerParameter("<br/><br/><hr><h3>MQTT server</h3>"),
  WiFiManagerParameter("mqttServer", "<br>IP", "", 16, "input='number' pattern='\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}'"),
  WiFiManagerParameter("mqttPort", "Port", "1883", 6, "pattern='\\d{1,5}'"),
  WiFiManagerParameter("mqttUser", "User", "", 255),
  WiFiManagerParameter("mqttPassword", "Password", "", 255, "type='password'"),
};

WiFiManagerParameter HassioParams[] =
{
  // The broker parameters
  WiFiManagerParameter("<br/><br/><hr><h3>HA Autodiscovery</h3>"),
  WiFiManagerParameter("hassSend", "", "Y", 2, "type='hidden'"),
  WiFiManagerParameter("<input type='checkbox' id='cbhas' onchange='getE(\"hassSend\").value = this.checked ? \"Y\":\"N\"'> <label for='cbhas'>Send</label>"),
  WiFiManagerParameter("hassDisc", "<br>Discovery Topic", MQTT_DISCOVERY_TOPIC, 32),
};

WiFiManagerParameter CloudAPI[] = 
{
  // The broker parameters
  WiFiManagerParameter("<br/><br/><hr><h3>CloudAPI</h3>"),
  WiFiManagerParameter("apiID", "IP", "", 16, "pattern='\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}'"),
  WiFiManagerParameter("apiSecret", "Port", "1883", 6, "pattern='\\d{1,5}'"),
};

WiFiManagerParameter radmonParams[] = 
{
  WiFiManagerParameter("<br/><br/><hr><h3>Radmon.org parameters</h3>"),
  WiFiManagerParameter("radmonSend", "", "Y", 2, "type='hidden'"),
  WiFiManagerParameter("<input type='checkbox' id='cbrm' onchange='getE(\"radmonSend\").value = this.checked ? \"Y\":\"N\"'> <label for='cbrm'>Send</label>"),
  WiFiManagerParameter("radmonUser", "<br>Radmon Username", "", 100),
  WiFiManagerParameter("radmonKey", "Radmon Data PW", "", 100, "type='password'"),
};

WiFiManagerParameter GMCParams[] = 
{
  WiFiManagerParameter("<br/><br/><hr><h3>GMC parameters</h3>"),
  WiFiManagerParameter("gmcSend", "", "Y", 2, "type='hidden'"),
  WiFiManagerParameter("<input type='checkbox' id='cbgm' onchange='getE(\"gmcSend\").value = this.checked ? \"Y\":\"N\"'> <label for='cbgm'>Send</label>"),
  WiFiManagerParameter("gmcAID", "<br>Account ID", "", 12, "pattern='\\d{1,5}'"),
  WiFiManagerParameter("gmcGCID", "Geiger Counter ID", "", 12, "pattern='\\d{1,12}'"),
  WiFiManagerParameter(R"J(<script>getE("cbgm").checked="Y"==getE("gmcSend").value,getE("cbrm").checked="Y"==getE("radmonSend").value,getE("cbts").checked="Y"==getE("tsSend").value,getE("cbhas").checked="Y"==getE("hassSend").value;</script>)J"),
};

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
  WiFiManager::autoConnect(hostName);
  Log::console(PSTR("WiFi: IP: %s"), WiFi.localIP().toString().c_str());
}

void ConfigManager::startWebPortal()
{
  for (int i = 0; i < sizeof(ESPGeigerParams) / sizeof(WiFiManagerParameter); i++)
    WiFiManager::addParameter(&ESPGeigerParams[i]);
  for (int i = 0; i < sizeof(MQTTParams) / sizeof(WiFiManagerParameter); i++)
    WiFiManager::addParameter(&MQTTParams[i]);
  for (int i = 0; i < sizeof(HassioParams) / sizeof(WiFiManagerParameter); i++)
    WiFiManager::addParameter(&HassioParams[i]);
  for (int i = 0; i < sizeof(radmonParams) / sizeof(WiFiManagerParameter); i++)
    WiFiManager::addParameter(&radmonParams[i]);
  for (int i = 0; i < sizeof(TSParams) / sizeof(WiFiManagerParameter); i++)
    WiFiManager::addParameter(&TSParams[i]);
  for (int i = 0; i < sizeof(GMCParams) / sizeof(WiFiManagerParameter); i++)
    WiFiManager::addParameter(&GMCParams[i]);

  ConfigManager::loadParams();

  const char* ratioChar = ConfigManager::getParamValueFromID("geigerRatio");
  double ratio = atof(ratioChar);
  gcounter.set_ratio(ratio);

  WiFiManager::setCustomMenuHTML("<form action='/status' method='get'><button>Status</button></form><br/>");
  const char * menu[] = {"custom", "wifi","param","info","update"};
  ConfigManager::setMenu(menu,sizeof(menu));
  WiFiManager::setWebServerCallback(std::bind(&ConfigManager::bindServerCallback, this));
  WiFiManager::setSaveParamsCallback(std::bind(&ConfigManager::saveParams, this));
  WiFiManager::startWebPortal();
}

void ConfigManager::handleRoot() {
  String page = FPSTR(HTTP_HEAD_START);
  page += FPSTR(HTTP_STYLE);
  page += FPSTR(faviconHead);
  page += FPSTR(HTTP_HEAD_END);
  page.replace(FPSTR(T_v), hostName);
  String str  = FPSTR(HTTP_ROOT_MAIN); // @todo custom title
  str.replace(FPSTR(T_t),thingName);
  str.replace(FPSTR(T_v), String(hostName) + " - " + WiFi.localIP().toString()); // use ip if ap is not active for heading @todo use hostname?

  page += str;
  page += FPSTR(HTTP_PORTAL_OPTIONS);

  page += "<form action='/status' method='get'><button>Status</button></form><br/>";
  page += HTTP_PORTAL_MENU[0];
  page += HTTP_PORTAL_MENU[3];
  page += HTTP_PORTAL_MENU[2];
  page += HTTP_PORTAL_MENU[8];
  str = FPSTR(HTTP_STATUS_ON);
  str.replace(FPSTR(T_i),WiFi.localIP().toString());
  str.replace(FPSTR(T_v),htmlEntities(WiFiManager::getWiFiSSID()));
  page += str;
  page += FPSTR(HTTP_END);

  ConfigManager::server->send(200, FPSTR(HTTP_HEAD_CT), page);

}

void ConfigManager::handleJSReturn()
{
  String page = FPSTR(picographJS);
  page += FPSTR(statusJS);
  ConfigManager::server.get()->send ( 200, FPSTR(HTTP_HEAD_CTJS), page);
}

void ConfigManager::handleJsonReturn()
{
  char jsonBuffer[1500] = "";

  int total = sizeof(jsonBuffer);
  int rcpm = round((status.geigerTicks.get()*60));
  int fivercpm = round(status.geigerTicks5.get()*60);
  int fifteenrcpm = round(status.geigerTicks15.get()*60);
  const char* ratioChar = ConfigManager::getParamValueFromID("geigerRatio");

  sprintf_P (
    jsonBuffer,
    PSTR("{\"uptime\": \"%s\", \"cpm\": %d, \"cpm5\": %d, \"cpm15\": %d, \"ratio\": %s}"),
    ConfigManager::getUptimeString(),
    rcpm,
    fivercpm,
    fifteenrcpm,
    ratioChar
  );
  jsonBuffer[sizeof(jsonBuffer)-1] = '\0';
  ConfigManager::server.get()->send ( 200, FPSTR(HTTP_HEAD_CTJSON), jsonBuffer );
}

void ConfigManager::handleStatusPage()
{
  String page = FPSTR(HTTP_HEAD_START);
  page += FPSTR(HTTP_STYLE);
  page += FPSTR(faviconHead);
  page += FPSTR(HTTP_HEAD_END);
  page += FPSTR(STATUS_PAGE_BODY);
  String title = FPSTR(thingName);
  title += F(" - Status");
  page.replace(FPSTR(T_v), title);
  page += FPSTR(HTTP_BACKBTN);
  page += FPSTR(STATUS_PAGE_FOOT);
  page.replace(FPSTR(T_1), String(status.thingName));
  page.replace(FPSTR(T_2), String(status.version));
  page.replace(FPSTR(T_3), String(status.git_version));
  page += FPSTR(HTTP_END);

  ConfigManager::server->send(200, FPSTR(HTTP_HEAD_CT), page);
}

void ConfigManager::handleRestart()
{
  String page = FPSTR(HTTP_HEAD_START);
  page += FPSTR(HTTP_STYLE);
  page += FPSTR(faviconHead);
  page += FPSTR(HTTP_HEAD_MREFRESH);
  page += FPSTR(HTTP_HEAD_END);
  page += FPSTR(thingName);
  page += F(" is restarting...<br /><br/>");
  page.replace(FPSTR(T_v), "Restarting ...");
  page += FPSTR(HTTP_END);
  ConfigManager::server->send(200, FPSTR(HTTP_HEAD_CT), page);
  Log::console(PSTR("Config: Restarting ... "));
  delay(1000);
  ESP.restart();
}

void ConfigManager::resetSettings()
{
  Log::console(PSTR("Config: Erasing ... "));
  LittleFS.begin();
  LittleFS.remove("/geigerconfig.json");
  LittleFS.end();
  WiFiManager::resetSettings();
}

void ConfigManager::handleResetParams()
{
  Log::console(PSTR("Config: Erasing ... "));
  LittleFS.begin();
  LittleFS.remove("/geigerconfig.json");
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
      Log::console(PSTR("Config: failed to open geigerconfig.json file"));
    }
  }
  else
  {
    Log::console(PSTR("Config: No geigerconfig.json file, using defaults"));
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
  MQTT_Client& mqtt = MQTT_Client::getInstance();
  const char* _send = getParamValueFromID("hassSend");
  if ((_send == NULL) || (strcmp(_send, "N") == 0)) {
    mqtt.removeHASSConfig();
  }
  mqtt.disconnect();
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