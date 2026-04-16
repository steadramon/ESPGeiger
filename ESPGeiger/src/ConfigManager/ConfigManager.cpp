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
#include "logos.h"
#include "html.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Mqtt/MQTT_Client.h"
#ifdef RADMONOUT
#include "../Radmon/Radmon.h"
#endif
#ifdef THINGSPEAKOUT
#include "../Thingspeak/Thingspeak.h"
#endif
#ifdef GMCOUT
#include "../GMC/GMC.h"
#endif
#ifdef WEBHOOKOUT
#include "../Webhook/Webhook.h"
#endif
#include "ArduinoJson.h"
#include <FS.h>
#include <LittleFS.h>
#include "../NTP/timezones.h"

#define _STR(x) #x
#define STR(x) _STR(x)

static WiFiManagerParameter ESPGeigerParams[] =
{
  // The broker parameters
  WiFiManagerParameter("<h1>ESPGeiger Config</h1>"),
  WiFiManagerParameter("geigerModel", "Model", GEIGER_MODEL, 32),
  WiFiManagerParameter("geigerRatio", "Ratio for calculating uSv", "151.0", 8, "required"),
  WiFiManagerParameter("geigerWarn", "Warning CPM", "50", 4, "pattern='\\d{1,4}'"),
  WiFiManagerParameter("geigerAlert", "Alert CPM", "100", 4, "pattern='\\d{1,4}'"),
#ifndef RXPIN_BLOCKED
  WiFiManagerParameter("geigerRX", "RX Pin", STR(GEIGER_RXPIN), 2),
#endif
#if defined(GEIGER_TXPIN) && GEIGER_TXPIN != -1
#ifndef TXPIN_BLOCKED
  WiFiManagerParameter("geigerTX", "TX Pin", STR(GEIGER_TXPIN), 2),
#endif
#endif
#ifdef USE_PCNT
  WiFiManagerParameter("pcntFilter", "PCNT Filter (0-1023, 0=off)", "100", 4, "type='number' min='0' max='1023'"),
  // 0=floating, 1=pull-up, 2=pull-down. Default comes from compile-time flags.
  WiFiManagerParameter("pcntPull", "PCNT Pin Pull (0=none, 1=up, 2=down)", STR(PCNT_PIN_PULL_DEFAULT), 2, "type='number' min='0' max='2'"),
#endif
#if GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(USE_PCNT)
  WiFiManagerParameter("geigerDebounce", "Debounce (us)", STR(GEIGER_DEBOUNCE), 5, "type='number' min='0' max='10000'"),
#endif
#if defined(SSD1306_DISPLAY) || defined(GEIGER_NEOPIXEL)
  WiFiManagerParameter("<br><hr><h3>Display</h3>"),
#endif
#if defined(SSD1306_DISPLAY) && defined(GEIGER_PUSHBUTTON)
  WiFiManagerParameter("dispTimeout", "Display timeout (s)", "120", 6, "required type='number' min='0' max='99999'"),
#endif
#if defined(SSD1306_DISPLAY)
  WiFiManagerParameter("dispBrightness", "Display brightness", "25", 4, "required type='number' min='0' max='100'"),
  #ifndef GEIGER_PUSHBUTTON
  WiFiManagerParameter("oledOn", "On Time", "06:00", 6, "type='time'"),
  WiFiManagerParameter("oledOff", "Off Time", "22:00", 6, "type='time'"),
  #endif
#endif
#ifdef GEIGER_NEOPIXEL
  WiFiManagerParameter("neopixelBrightness", "NeoPixel brightness", "15", 4, "required type='number' min='0' max='100'"),
#endif
};
#ifdef THINGSPEAKOUT
static WiFiManagerParameter TSParams[] = 
{
  // Thingspeak parameters
  WiFiManagerParameter("<br><hr><h3>Thingspeak</h3>"),
  WiFiManagerParameter("tsSend", "", "N", 2, "type='hidden'"),
  WiFiManagerParameter(R"J(<input type='checkbox' id='cbts' onchange='setCB("tsSend",this)'> <label for='cbts'>Send</label><br><script>doCB("cbts","tsSend")</script>)J"),
  WiFiManagerParameter("tsChannelKey", "Channel Key", "", 16),
};
#endif
#ifdef MQTTOUT
static WiFiManagerParameter MQTTParams[] = 
{
  // The broker parameters
  WiFiManagerParameter("<br><hr><h3>MQTT</h3>"),
  WiFiManagerParameter("mqttServer", "<br>IP", "", 16, "input='number' pattern='\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}'"),
  WiFiManagerParameter("mqttPort", "Port", "1883", 6, "type='number' min='1' max='65535'"),
  WiFiManagerParameter("mqttUser", "User", "", 32),
  WiFiManagerParameter("mqttPassword", "Password", "", 32, "type='password'"),
  WiFiManagerParameter("mqttTopic", "Root Topic", "ESPGeiger-{id}", 16),
  WiFiManagerParameter("<small>{id} is replaced with the chip ID</small><br>"),
  WiFiManagerParameter("mqttTime", "Submit Time (s)", "60", 6, "required type='number' min='5' max='3600'"),
};
#ifdef MQTTAUTODISCOVER
static WiFiManagerParameter HassioParams[] = 
{
  // The broker parameters
  WiFiManagerParameter("<br><hr><h3>HA Autodiscovery</h3>"),
  WiFiManagerParameter("hassSend", "", MQTT_DISCOVERY, 2, "type='hidden'"),
  WiFiManagerParameter(R"J(<input type='checkbox' id='cbhas' onchange='setCB("hassSend",this)'> <label for='cbhas'>Send</label><br><script>doCB("cbhas","hassSend")</script>)J"),
  WiFiManagerParameter("hassDisc", "Discovery Topic", S_MQTT_DISCOVERY_TOPIC, 32),
};
#endif
#endif
static WiFiManagerParameter CloudAPI[] = 
{
  WiFiManagerParameter("<br><hr><h3>CloudAPI</h3>"),
  WiFiManagerParameter("apiID", "IP", "", 16, "pattern='\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}'"),
  WiFiManagerParameter("apiSecret", "Port", "1883", 6, "pattern='\\d{1,5}'"),
};
#ifdef RADMONOUT
static WiFiManagerParameter radmonParams[] = 
{
  WiFiManagerParameter("<br><hr><h3>Radmon.org</h3>"),
  WiFiManagerParameter("radmonSend", "", "N", 2, "type='hidden'"),
  WiFiManagerParameter(R"J(<input type='checkbox' id='cbrm' onchange='setCB("radmonSend",this)'> <label for='cbrm'>Send</label><br><script>doCB("cbrm","radmonSend")</script>)J"),
  WiFiManagerParameter("radmonUser", "Radmon Username", "", 32),
  WiFiManagerParameter("radmonKey", "Radmon Data PW", "", 64, "type='password'"),
  WiFiManagerParameter("radmonTime", "Submit Time (s)", "60", 6, "required type='number' min='30' max='1800'"),
};
#endif
#ifdef GMCOUT
static WiFiManagerParameter GMCParams[] = 
{
  WiFiManagerParameter("<br/><br/><hr><h3>GMC</h3>"),
  WiFiManagerParameter("gmcSend", "", "N", 2, "type='hidden'"),
  WiFiManagerParameter(R"J(<input type='checkbox' id='cbgm' onchange='setCB("gmcSend",this)'> <label for='cbgm'>Send</label><br><script>doCB("cbgm","gmcSend")</script>)J"),
  WiFiManagerParameter("gmcAID", "Account ID", "", 12, "pattern='\\d{1,5}'"),
  WiFiManagerParameter("gmcGCID", "Geiger Counter ID", "", 12, "pattern='\\d{1,12}'"),
};
#endif

#ifdef WEBHOOKOUT
static WiFiManagerParameter WHParams[] =
{
  WiFiManagerParameter("<br><hr><h3>Webhook</h3>"),
  WiFiManagerParameter("whSend", "", "N", 2, "type='hidden'"),
  WiFiManagerParameter(R"J(<input type='checkbox' id='cbwh' onchange='setCB("whSend",this)'> <label for='cbwh'>Send</label><br><script>doCB("cbwh","whSend")</script>)J"),
  WiFiManagerParameter("whURL", "Webhook URL", "", 255),
  WiFiManagerParameter("whKey", "Webhook Key", "", 255),
  WiFiManagerParameter("whTime", "Submit Time (s)", "60", 5, "required type='number' min='1' max='3600'"),
};
#endif

ConfigManager::ConfigManager() : WiFiManager(){

#ifdef ESP8266
  uint32_t tchipId = ESP.getChipId();
#elif defined(ESP32)
  uint32_t tchipId = 0;
  for (int i=0; i<17; i=i+8) {
   tchipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
#endif
  String hexId = String(tchipId & 0xFFFFFF, HEX);
  strncpy(chipId, hexId.c_str(), sizeof(chipId) - 1);
  chipId[sizeof(chipId) - 1] = '\0';

  snprintf_P (hostName, sizeof(hostName), PSTR("%S-%S"), status.thingName, chipId);
  snprintf_P (userAgent, sizeof(userAgent), PSTR("%S/%S (%S; %S; %S; %S)"), status.thingName, status.version, status.git_version, status.geiger_model, GetChipModel(), chipId);

  setHostname(hostName);
  setTitle(status.thingName);
  setCustomHeadElement(faviconHead);
}
ParsedTime ConfigManager::parseTime(const char* timeStr) {
    ParsedTime pt;
    pt.hour = -1;
    pt.minute = -1;
    pt.isValid = false;

    if (timeStr == NULL || strlen(timeStr) != 5 || timeStr[2] != ':') {
        return pt; // Invalid format
    }

    // Create a mutable copy of the string as strtok modifies it
    char tempStr[6]; // HH:MM\0
    strncpy(tempStr, timeStr, sizeof(tempStr) - 1);
    tempStr[sizeof(tempStr) - 1] = '\0'; // Ensure null-termination

    char* hourStr = strtok(tempStr, ":");
    char* minStr = strtok(NULL, ":");

    if (hourStr == NULL || minStr == NULL) {
        return pt; // Parsing failed
    }

    int h = atoi(hourStr);
    int m = atoi(minStr);

    // Basic validation for hours and minutes
    if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
        pt.hour = h;
        pt.minute = m;
        pt.isValid = true;
    }

    return pt;
}

void ConfigManager::getOurParamOut(){

  if(_paramsCount > 0){

    String HTTP_PARAM_temp = FPSTR(HTTP_FORM_LABEL);
    HTTP_PARAM_temp += FPSTR(HTTP_FORM_PARAM);
    bool tok_I = HTTP_PARAM_temp.indexOf(FPSTR(T_I)) > 0;
    bool tok_i = HTTP_PARAM_temp.indexOf(FPSTR(T_i)) > 0;
    bool tok_n = HTTP_PARAM_temp.indexOf(FPSTR(T_n)) > 0;
    bool tok_p = HTTP_PARAM_temp.indexOf(FPSTR(T_p)) > 0;
    bool tok_t = HTTP_PARAM_temp.indexOf(FPSTR(T_t)) > 0;
    bool tok_l = HTTP_PARAM_temp.indexOf(FPSTR(T_l)) > 0;
    bool tok_v = HTTP_PARAM_temp.indexOf(FPSTR(T_v)) > 0;
    bool tok_c = HTTP_PARAM_temp.indexOf(FPSTR(T_c)) > 0;

    char valLength[5];

    for (int i = 0; i < _paramsCount; i++) {
      //Serial.println((String)_params[i]->_length);
      if (_params[i] == NULL || _params[i]->getValueLength() > 99999) {
        // try to detect param scope issues, doesnt always catch but works ok
        return;
      }
    }

    // add the extra parameters to the form
    for (int i = 0; i < _paramsCount; i++) {
     // label before or after, @todo this could be done via floats or CSS and eliminated
     String pitem;
      switch (_params[i]->getLabelPlacement()) {
        case WFM_LABEL_BEFORE:
          pitem = FPSTR(HTTP_FORM_LABEL);
          pitem += FPSTR(HTTP_FORM_PARAM);
          break;
        case WFM_LABEL_AFTER:
          pitem = FPSTR(HTTP_FORM_PARAM);
          pitem += FPSTR(HTTP_FORM_LABEL);
          break;
        default:
          // WFM_NO_LABEL
          pitem = FPSTR(HTTP_FORM_PARAM);
          break;
      }

      // Input templating
      // "<br/><input id='{i}' name='{n}' maxlength='{l}' value='{v}' {c}>";
      // if no ID use customhtml for item, else generate from param string
      if (_params[i]->getID() != NULL) {
        if(tok_I)pitem.replace(FPSTR(T_I), (String)FPSTR(S_parampre)+(String)i); // T_I id number
        if(tok_i)pitem.replace(FPSTR(T_i), _params[i]->getID()); // T_i id name
        if(tok_n)pitem.replace(FPSTR(T_n), _params[i]->getID()); // T_n id name alias
        if(tok_p)pitem.replace(FPSTR(T_p), FPSTR(T_t)); // T_p replace legacy placeholder token
        if(tok_t)pitem.replace(FPSTR(T_t), _params[i]->getLabel()); // T_t title/label
        snprintf(valLength, 5, "%d", _params[i]->getValueLength());
        if(tok_l)pitem.replace(FPSTR(T_l), valLength); // T_l value length
        if(tok_v)pitem.replace(FPSTR(T_v), _params[i]->getValue()); // T_v value
        if(tok_c)pitem.replace(FPSTR(T_c), _params[i]->getCustomHTML()); // T_c meant for additional attributes, not html, but can stuff
      } else {
        server->sendContent(_params[i]->getCustomHTML());
        continue;
      }
      server->sendContent(pitem);
    }
  }
}

void ConfigManager::handleOurParam(){
  handleRequest();
  String page = getHTTPHead(FPSTR(S_titleparam)); // @token titlewifi
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->sendHeader(F("Pragma"), F("no-cache"));
  server->sendHeader(F("Expires"), F("-1"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, FPSTR(HTTP_HEAD_CT), "");
  page = FPSTR(HTTP_HEAD_START);
  page.replace(FPSTR(T_v), FPSTR(S_titleparam));
  server->sendContent(page);
  server->sendContent(FPSTR(HTTP_STYLE));
  server->sendContent(FPSTR(faviconHead));
  server->sendContent(FPSTR(CFG_PAGE_JS));
  server->sendContent(FPSTR(HTTP_HEAD_END));
  page = FPSTR(HTTP_FORM_START);
  page.replace(FPSTR(T_v), F("paramsave"));
  server->sendContent(page);
  getOurParamOut();
  server->sendContent(FPSTR(HTTP_FORM_END));
  server->sendContent(FPSTR(HTTP_BACKBTN));
  server->sendContent(FPSTR(HTTP_END));

  server->sendContent("");
  server->client().stop();
}

bool ConfigManager::autoConnect()
{
  WiFiManager::setConnectTimeout(10);
  WiFiManager::setAPClientCheck(true);
  WiFiManager::setWiFiAutoReconnect(true);
  WiFiManager::setConfigPortalTimeout(300);
#ifdef ESP8266
  if (WiFiManager::getWiFiIsSaved()) {
    WiFiManager::setEnableConfigPortal(false);
  }
#endif
  bool result = WiFiManager::autoConnect(hostName);

  if (result) {
    Log::console(PSTR("WiFi: IP: %s"), WiFi.localIP().toString().c_str());
    return result;
  }

  uint8_t connection_result = WiFiManager::getLastConxResult();
  if (connection_result == WL_STATION_WRONG_PASSWORD) {
    Log::console(PSTR("Config: WiFi password incorrect"));
    WiFiManager::setEnableConfigPortal(true);
    WiFiManager::setConfigPortalTimeout(300);
    Log::console(PSTR("Config: Entering setup for 300s"));
#if defined(SSD1306_DISPLAY)
    display.setupWifi(hostName);
#endif
    result = WiFiManager::autoConnect(hostName);
    if (!result) {
      Log::console(PSTR("WiFi password incorrect ... Restarting ... "));
      delay(1000);
      ESP.restart();
    }
    return result;
  }
  if (connection_result == WL_CONNECT_FAILED) {
      Log::console(PSTR("Connection FAILED ... Restarting ... "));
      delay(1000);
      ESP.restart();
  }

  if (WiFiManager::getWiFiIsSaved()) {
    WiFiManager::setEnableConfigPortal(true);
    WiFiManager::setConfigPortalTimeout(90);
    Log::console(PSTR("Config: Entering setup for 90s"));
#if defined(SSD1306_DISPLAY)
    display.setupWifi(hostName);
#endif
    result = WiFiManager::autoConnect(hostName);
  }

  return result;
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
#ifdef WEBHOOKOUT
  for (int i = 0; i < sizeof(WHParams) / sizeof(WiFiManagerParameter); i++)
    WiFiManager::addParameter(&WHParams[i]);
#endif

  ConfigManager::loadParams();
  const char* cfgvar;
  int cfgint;

#ifndef RXPIN_BLOCKED
  cfgvar = ConfigManager::getParamValueFromID("geigerRX");
  if (cfgvar != NULL) {
    cfgint = atoi(cfgvar);
    gcounter.set_rx_pin(cfgint);
  }
#endif
#if defined(GEIGER_TXPIN) && GEIGER_TXPIN != -1
#ifndef TXPIN_BLOCKED
  cfgvar = ConfigManager::getParamValueFromID("geigerTX");
  if (cfgvar != NULL) {
    cfgint = atoi(cfgvar);
    gcounter.set_tx_pin(cfgint);
  }
#endif
#endif
  ConfigManager::setExternals();

  WiFiManager::setWebServerCallback(std::bind(&ConfigManager::bindServerCallback, this));
  WiFiManager::setSaveParamsCallback(std::bind(&ConfigManager::saveParams, this));
  WiFiManager::startWebPortal();
}

void ConfigManager::setExternals() {
  const char* cfgvar;
  cfgvar = ConfigManager::getParamValueFromID("geigerRatio");
  if (cfgvar == NULL) {
    cfgvar = "151.0";
  }
  float ratio = atof(cfgvar);
  gcounter.set_ratio(ratio);

  cfgvar = ConfigManager::getParamValueFromID("geigerWarn");
  if (cfgvar == NULL) {
    cfgvar = "50";
  }
  int cfgint = atoi(cfgvar);
  gcounter.set_warning(cfgint);

  cfgvar = ConfigManager::getParamValueFromID("geigerAlert");
  if (cfgvar == NULL) {
    cfgvar = "100";
  }
  cfgint = atoi(cfgvar);
  gcounter.set_alert(cfgint);

  // set_pcnt_filter / set_pin_pull are no-op virtuals on non-PCNT builds,
  // and the form params aren't registered there, so these are idempotent.
  cfgvar = ConfigManager::getParamValueFromID("pcntFilter");
  if (cfgvar != NULL) {
    gcounter.set_pcnt_filter(atoi(cfgvar));
  }
  cfgvar = ConfigManager::getParamValueFromID("pcntPull");
  if (cfgvar != NULL) {
    gcounter.set_pin_pull(atoi(cfgvar));
  }

#if GEIGER_IS_PULSE(GEIGER_TYPE) && !defined(USE_PCNT)
  cfgvar = ConfigManager::getParamValueFromID("geigerDebounce");
  if (cfgvar != NULL) {
    gcounter.set_debounce(atoi(cfgvar));
  }
#endif

#if defined(SSD1306_DISPLAY) && defined(GEIGER_PUSHBUTTON)
  cfgvar = ConfigManager::getParamValueFromID("dispTimeout");
  if (cfgvar == NULL) {
    cfgvar = "120";
  }
  display.setTimeout(atoi(cfgvar));
#endif
#if defined(SSD1306_DISPLAY)
  cfgvar = ConfigManager::getParamValueFromID("dispBrightness");
  if (cfgvar == NULL) {
    cfgvar = "64";
  }
  display.setBrightness(atoi(cfgvar));
#endif
#ifdef GEIGER_NEOPIXEL
  cfgvar = ConfigManager::getParamValueFromID("neopixelBrightness");
  if (cfgvar == NULL) {
    cfgvar = "15";
  }
  neopixel.setBrightness(atoi(cfgvar));
#endif
}

void ConfigManager::handleRoot() {
  handleRequest();
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->sendHeader(F("Pragma"), F("no-cache"));
  server->sendHeader(F("Expires"), F("-1"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, FPSTR(HTTP_HEAD_CT), "");
  server->client().flush();
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
  page.replace(FPSTR(T_t),status.thingName);
#endif
  char heading[64];
  snprintf(heading, sizeof(heading), "%s - %s", hostName, WiFi.localIP().toString().c_str());
  page.replace(FPSTR(T_v), heading);
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
  char jsonBuffer[320] = "";

  const char* ratioChar = ConfigManager::getParamValueFromID("geigerRatio");
  char c[16], s[16], c5[16], c15[16], cs[16];
  format_f(c, sizeof(c), gcounter.get_cpmf());
  format_f(s, sizeof(s), gcounter.get_usv());
  format_f(c5, sizeof(c5), gcounter.get_cpm5f());
  format_f(c15, sizeof(c15), gcounter.get_cpm15f());
  format_f(cs, sizeof(cs), gcounter.get_cps());
  int pos = snprintf_P (
    jsonBuffer,
    sizeof(jsonBuffer),
    PSTR("{\"u\":\"%s\",\"ut\":%lu,\"c\":%s,\"s\":%s,\"c5\":%s,\"c15\":%s,\"cs\":%s,\"r\":%s,\"tc\":%u,\"mem\":%u,\"rssi\":%d"),
    ConfigManager::getUptimeString(),
    ConfigManager::getUptime(),
    c, s, c5, c15, cs,
    ratioChar,
    gcounter.total_clicks,
    ESP.getFreeHeap(),
    (int)WiFi.RSSI()
  );
#ifdef ESPGEIGER_HW
  char hv[16];
  format_f(hv, sizeof(hv), status.hvReading.get());
  pos += snprintf_P(jsonBuffer + pos, sizeof(jsonBuffer) - pos, PSTR(",\"hv\":%s"), hv);
#endif
  pos += snprintf_P(jsonBuffer + pos, sizeof(jsonBuffer) - pos,
    PSTR(",\"tick\":%u,\"t_max\":%u,\"lps\":%u"),
    status.tick_us, status.tick_max_us, status.lps);
  snprintf_P(jsonBuffer + pos, sizeof(jsonBuffer) - pos, PSTR("}"));
  jsonBuffer[sizeof(jsonBuffer)-1] = '\0';
  ConfigManager::server.get()->send ( 200, FPSTR(HTTP_HEAD_CTJSON), jsonBuffer );
}

void ConfigManager::handleGeigerLog() {
  handleRequest();
  char jsonBuffer[128] = "";
  const char* ratioChar = ConfigManager::getParamValueFromID("geigerRatio");
  char cpm[16], cps[16];
  format_f(cpm, sizeof(cpm), gcounter.get_cpmf());
  format_f(cps, sizeof(cps), gcounter.get_cps());
  snprintf_P (
    jsonBuffer,
    sizeof(jsonBuffer),
    PSTR("%s, %s, nan, nan, nan, nan, nan, nan, nan, nan, nan, nan"),
    cpm, cps
  );
  jsonBuffer[sizeof(jsonBuffer)-1] = '\0';
  ConfigManager::server.get()->send ( 200, FPSTR(HTTP_HEAD_CT), jsonBuffer );
}

void ConfigManager::handleSerialOut() {
  handleRequest();
  if (status.serialOut == 0) {
    status.serialOut = 1;
    Log::setSerialLogLevel(false);
  } else {
    status.serialOut = 0;
    Log::setSerialLogLevel(true);
  }
  ConfigManager::server.get()->send ( 200, FPSTR(HTTP_HEAD_CT), "OK" );
}

void ConfigManager::handleAbout()
{
  handleRequest();
  char jsonBuffer[1024] = "";
  size_t pos = 0;

  uint8_t macb[6];
  WiFi.macAddress(macb);
  char mac_str[13];
  snprintf(mac_str, sizeof(mac_str), "%02X%02X%02X%02X%02X%02X",
    macb[0], macb[1], macb[2], macb[3], macb[4], macb[5]);

  pos += snprintf_P(jsonBuffer + pos, sizeof(jsonBuffer) - pos,
    PSTR("{\"ver\":\"%s\",\"git\":\"%s\",\"env\":\"%s\","
         "\"date\":\"%s %s\",\"chip\":\"%s\",\"mac\":\"%s\","
         "\"host\":\"%s\",\"g_mod\":\"%s\","
         "\"g_type\":\"%s\",\"g_test\":%s,\"g_pcnt\":%s,"
         "\"modules\":["),
    status.version, status.git_version, status.build_env,
    __DATE__, __TIME__,
    GetChipModel(),
    mac_str,
    hostName,
    status.geiger_model,
    GEIGER_IS_PULSE(GEIGER_TYPE)  ? "pulse" :
    GEIGER_IS_SERIAL(GEIGER_TYPE) ? "serial" : "none",
    GEIGER_IS_TEST(GEIGER_TYPE)   ? "true" : "false",
    gcounter.has_pcnt()           ? "true" : "false");

  uint8_t count = EGModuleRegistry::count();
  for (uint8_t i = 0; i < count; i++) {
    EGModule* m = EGModuleRegistry::get(i);
    if (m == nullptr) continue;
    pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos,
      "%s\"%s\"", (i > 0 ? "," : ""), m->name());
  }
  snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, "]}");
  ConfigManager::server.get()->send(200, FPSTR(HTTP_HEAD_CTJSON), jsonBuffer);
}

void ConfigManager::handleOutputsJson()
{
  handleRequest();
  char jsonBuffer[512] = "";
  size_t pos = 0;
  unsigned long now = millis();
  bool first = true;
  pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, "{");

  // Emits "name":{"ok":bool,"age":S|null} — only for enabled modules.
  // age is in seconds, or null when no attempt has been made yet.
  auto emit = [&](const char* name, bool enabled, bool last_ok, unsigned long last_attempt_ms) {
    if (!enabled) return;
    if (!first) {
      pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, ",");
    }
    first = false;
    if (last_attempt_ms == 0) {
      pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos,
        "\"%s\":{\"ok\":false,\"age\":null}",
        name);
    } else {
      pos += snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos,
        "\"%s\":{\"ok\":%s,\"age\":%lu}",
        name,
        last_ok ? "true" : "false",
        (now - last_attempt_ms) / 1000);
    }
  };

  auto send_enabled = [&](const char* paramId) {
    const char* s = getParamValueFromID(paramId);
    return (s != NULL) && (strcmp(s, "N") != 0);
  };

#ifdef MQTTOUT
  {
    const char* mqttServer = getParamValueFromID("mqttServer");
    bool enabled = (mqttServer != NULL) && (mqttServer[0] != '\0');
    MQTT_Client& mqtt = MQTT_Client::getInstance();
    emit("mqtt", enabled, mqtt.last_ok && status.mqtt_connected, mqtt.last_attempt_ms);
  }
#endif
#ifdef RADMONOUT
  emit("radmon", send_enabled("radmonSend"), radmon.last_ok, radmon.last_attempt_ms);
#endif
#ifdef THINGSPEAKOUT
  emit("thingspeak", send_enabled("tsSend"), thingspeak.last_ok, thingspeak.last_attempt_ms);
#endif
#ifdef GMCOUT
  emit("gmc", send_enabled("gmcSend"), gmc.last_ok, gmc.last_attempt_ms);
#endif
#ifdef WEBHOOKOUT
  emit("webhook", send_enabled("whSend"), webhook.last_ok, webhook.last_attempt_ms);
#endif

  snprintf(jsonBuffer + pos, sizeof(jsonBuffer) - pos, "}");
  ConfigManager::server.get()->send(200, FPSTR(HTTP_HEAD_CTJSON), jsonBuffer);
}

void ConfigManager::handleClicksReturn()
{
  handleRequest();
  DynamicJsonDocument json(512);

  json.clear();
  auto last_day = json.createNestedArray("last_day");
#if GEIGER_IS_TEST(GEIGER_TYPE)
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
  handleRequest();
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->sendHeader(F("Pragma"), F("no-cache"));
  server->sendHeader(F("Expires"), F("-1"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, FPSTR(HTTP_HEAD_CT), "");
  char title[48];
  String page = FPSTR(HTTP_HEAD_START);
  snprintf(title, sizeof(title), "%s - Status", hostName);
  page.replace(FPSTR(T_v), title);
  server->sendContent(page);
  server->sendContent(FPSTR(HTTP_STYLE));
  server->sendContent(FPSTR(faviconHead));
  server->sendContent(FPSTR(HTTP_HEAD_END));
  page = FPSTR(STATUS_PAGE_BODY_HEAD);
  snprintf(title, sizeof(title), "%s - Status", status.thingName);
  page.replace(FPSTR(T_v), title);
  page.replace(FPSTR(T_t), hostName);
  server->sendContent(page);
  server->sendContent(FPSTR(STATUS_PAGE_BODY));
  server->sendContent(FPSTR(HTTP_BACKBTN));
  page = FPSTR(STATUS_PAGE_FOOT);
  page.replace(FPSTR(T_1), status.version);
  server->sendContent(page);
  server->sendContent(FPSTR(HTTP_END));
  server->sendContent("");
  server->client().stop();
}

void ConfigManager::handleHistoryPage()
{
  handleRequest();
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->sendHeader(F("Pragma"), F("no-cache"));
  server->sendHeader(F("Expires"), F("-1"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, FPSTR(HTTP_HEAD_CT), "");
  char title[48];
  String page = FPSTR(HTTP_HEAD_START);
  snprintf(title, sizeof(title), "%s - History", hostName);
  page.replace(FPSTR(T_v), title);
  server->sendContent(page);
  server->sendContent(FPSTR(HTTP_STYLE));
  server->sendContent(FPSTR(faviconHead));
  server->sendContent(FPSTR(HTTP_HEAD_END));
  page = FPSTR(STATUS_PAGE_BODY_HEAD);
  snprintf(title, sizeof(title), "%s - History", status.thingName);
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
  handleRequest();
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, FPSTR(HTTP_HEAD_CT), "");
  char title[48];
  String page = FPSTR(HTTP_HEAD_START);
  snprintf(title, sizeof(title), "%s - NTP", status.thingName);
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
  handleRequest();
  char stmp[64];
  String s = server->arg("s");
  strncpy(stmp, s.c_str(), sizeof(stmp) - 1);
  stmp[sizeof(stmp) - 1] = '\0';
  ntpclient.set_server(stmp);
  s = server->arg("t");
  strncpy(stmp, s.c_str(), sizeof(stmp) - 1);
  stmp[sizeof(stmp) - 1] = '\0';
  ntpclient.set_tz(stmp);
  ntpclient.saveconfig();
  ntpclient.setup();

  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, FPSTR(HTTP_HEAD_CT), "");
  char title[48];
  String page = FPSTR(HTTP_HEAD_START);
  snprintf(title, sizeof(title), "%s - NTP Saved", status.thingName);
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
  handleRequest();
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->sendHeader(F("Pragma"), F("no-cache"));
  server->sendHeader(F("Expires"), F("-1"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, FPSTR(HTTP_HEAD_CT), "");
  char title[48];
  String page = FPSTR(HTTP_HEAD_START);
  snprintf(title, sizeof(title), "%s-HW - HV", status.thingName);
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
  handleRequest();
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
  handleRequest();
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
  handleRequest();
  char jsonBuffer[256] = "";

  int total = sizeof(jsonBuffer);
  int volts = status.hvReading.get();
  int freq = hardware.get_freq();
  int duty = hardware.get_duty();
  int ratio = hardware.get_vd_ratio();

  snprintf_P (
    jsonBuffer,
    sizeof(jsonBuffer),
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
#if GEIGER_IS_TEST(GEIGER_TYPE)
void ConfigManager::handleSetCPM() {
  handleRequest();
  int _d = atoi(server->arg(F("v")).c_str());
  if (_d > 0) {
    gcounter.set_target_cpm(_d);
  }
  ConfigManager::server->send(200, FPSTR(HTTP_HEAD_CT), F("OK"));
}
#endif

void ConfigManager::handleRestart()
{
  handleRequest();
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
  server->sendContent(FPSTR(status.thingName));
  server->sendContent(F(" is restarting...<br><br>"));
  server->sendContent(FPSTR(HTTP_END));
  server->sendContent("");
  server->client().stop();
  Log::console(PSTR("Config: Restarting ... "));
  delay(1000);
  ESP.restart();
}

void ConfigManager::eraseSettings()
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

// Self-throttled wrapper around WiFiManager::process().
//   ESP32: sync WebServer.handleClient() yields 1ms per call via lwIP
//     socket timeouts, capping loop() LPS to ~1k if unthrottled. 10 ms
//     polling (100 Hz) keeps HTTP responsive with huge loop headroom.
//   ESP8266: handler is cheap per call but not free (~3-5 µs each) —
//     still meaningful at 20-50k iter/sec. 5 ms keeps captive-portal DNS
//     snappy without eating ~10% of CPU on idle polling.
// Override either via -D CMAN_PROCESS_INTERVAL_MS=N.
#ifndef CMAN_PROCESS_INTERVAL_MS
#ifdef ESP32
#define CMAN_PROCESS_INTERVAL_MS 10
#else
#define CMAN_PROCESS_INTERVAL_MS 5
#endif
#endif
void ConfigManager::processLoop(unsigned long now)
{
  static unsigned long last = 0;
  if (now - last >= CMAN_PROCESS_INTERVAL_MS) {
    last = now;
    this->process();
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
      DynamicJsonDocument jsonBuffer(3072);
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
      jsonBuffer.clear();
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
  handleRequest();
  uint32_t counter = 0;

  char stmp[8];
  String s = server->arg("c1");
  strncpy(stmp, s.c_str(), sizeof(stmp) - 1);
  stmp[sizeof(stmp) - 1] = '\0';
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
  char idxBuf[8];
  snprintf(idxBuf, sizeof(idxBuf), "%u\n", (uint8_t)Log::getLogIdx());
  server->sendContent(idxBuf);
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
        char stemp[256];
        size_t copyLen = (len < sizeof(stemp) - 1) ? len : sizeof(stemp) - 1;
        memcpy(stemp, tmp, copyLen);
        stemp[copyLen - 1] = '\n';
        stemp[copyLen] = '\0';
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
    snprintf(tmp, sizeof(tmp), "\"%s\":\"%s\"",customParams[i]->getID(),customParams[i]->getValue());
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
  const char* _rx = ConfigManager::getParamValueFromID("geigerRX");
  if (_rx != NULL) {
    int rx_pin = atoi(_rx);
    if (rx_pin != gcounter.get_rx_pin()) {
      Log::console(PSTR("Config: Geiger RX Pin Changed"));
      restart_required = true;
    }
  }
#endif
#if defined(GEIGER_TXPIN) && GEIGER_TXPIN != -1
#ifndef TXPIN_BLOCKED
  const char* _tx = ConfigManager::getParamValueFromID("geigerTX");
  if (_tx != NULL) {
    int tx_pin = atoi(_tx);
    if (tx_pin != gcounter.get_tx_pin()) {
      Log::console(PSTR("Config: Geiger TX Pin"));
      restart_required = true;
    }
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
  if (((_send == NULL) || (strcmp(_send, "N") == 0)) && (status.mqtt_connected)) {
    mqtt.removeHASSConfig();
  }
#endif
  mqtt.disconnect();
  mqtt.begin();
#endif
  ConfigManager::setExternals();
  gcounter.apply_pcnt_filter();
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

const char* ConfigManager::getParamValueFromID_P(const __FlashStringHelper *param_in)
{
  WiFiManagerParameter** customParams = ConfigManager::getParameters();
  for (int i = 0; i < ConfigManager::getParametersCount(); i++)
  {
    if (customParams[i]->getID() == NULL)
      continue;
    if (strncmp_P(customParams[i]->getID(), (const char*)param_in, strlen_P((const char*)param_in)) == 0)
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
  ConfigManager::server.get()->on(WM_G(R_param), std::bind(&ConfigManager::handleOurParam, this));
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
  ConfigManager::server.get()->on(GEIGERLOG_URL, HTTP_GET, std::bind(&ConfigManager::handleGeigerLog, this));
  ConfigManager::server.get()->on(SERIAL_URL, HTTP_GET, std::bind(&ConfigManager::handleSerialOut, this));
  ConfigManager::server.get()->on(ABOUT_URL, HTTP_GET, std::bind(&ConfigManager::handleAbout, this));
  ConfigManager::server.get()->on(OUTPUTS_URL, HTTP_GET, std::bind(&ConfigManager::handleOutputsJson, this));
#ifdef ESPGEIGER_HW
  ConfigManager::server.get()->on(HV_URL, HTTP_GET, std::bind(&ConfigManager::handleHVPage, this));
  ConfigManager::server.get()->on(HV_SET_URL, HTTP_GET, std::bind(&ConfigManager::handleHVSet, this));
  ConfigManager::server.get()->on(HV_JS_URL, HTTP_GET, std::bind(&ConfigManager::handleHVJSReturn, this));
  ConfigManager::server.get()->on(HV_JSON_URL, HTTP_GET, std::bind(&ConfigManager::handleHVJsonReturn, this));
#endif
#if GEIGER_IS_TEST(GEIGER_TYPE)
  ConfigManager::server.get()->on(SETCPM_URL, HTTP_GET, std::bind(&ConfigManager::handleSetCPM, this));
#endif
}

unsigned long ConfigManager::getUptime () {
  time_t uptime = NTP.getUptime ();
  return uptime;
}

char* ConfigManager::getUptimeString () {
    static char uptimeBuffer[20];
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

    snprintf_P (uptimeBuffer, sizeof (uptimeBuffer), PSTR("%dT%02d:%02d:%02d"), days, hours, minutes, seconds);

    return uptimeBuffer;
}
