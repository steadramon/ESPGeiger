/*
  ConfigManager.h - Config Manager class
  
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

#ifndef ConfigManager_h
#define ConfigManager_h
#include "WiFiManager.h"
#include "../Util/Globals.h"
#include "../Util/DeviceInfo.h"
#include "../Util/StringUtil.h"
#include "../Counter/Counter.h"
#include "../NTP/NTP.h"

#ifdef ESPGEIGER_HW
#include "../ESPGHW/ESPGHW.h"
extern ESPGeigerHW hardware;
#endif

extern Counter gcounter;
extern NTP_Client ntpclient;
constexpr auto MQTT_SERVER_LENGTH = 31;
constexpr auto MQTT_PORT_LENGTH = 6;
constexpr auto MQTT_USER_LENGTH = 31;
constexpr auto MQTT_PASS_LENGTH = 31;
constexpr auto CHECKBOX_LENGTH = 9;
constexpr auto NUMBER_LEN = 32;
constexpr auto TEMPLATE_LEN = 256;
constexpr auto ADVANCED_LEN = 256;
constexpr auto CB_SELECTED_STR = "selected";

constexpr auto ROOT_URL PROGMEM = "/";
constexpr auto RESTART_URL PROGMEM = "/restart";
constexpr auto CONSOLE_URL PROGMEM = "/cs";
constexpr auto STATUS_URL PROGMEM = "/status";
constexpr auto JS_URL PROGMEM = "/js";
constexpr auto JSON_URL PROGMEM = "/json";
constexpr auto RESET_URL PROGMEM = "/reset";
constexpr auto CLICKS_JSON PROGMEM = "/clicks";
constexpr auto NTP_URL PROGMEM = "/ntp";
constexpr auto NTP_SET_URL PROGMEM = "/ntpset";
constexpr auto HIST_URL PROGMEM = "/hist";
constexpr auto GEIGERLOG_URL PROGMEM = "/lastdata";
constexpr auto SERIAL_URL PROGMEM = "/serial";
constexpr auto ABOUT_URL PROGMEM = "/about";
constexpr auto INFO_URL  PROGMEM = "/info";
constexpr auto OUTPUTS_URL PROGMEM = "/outputs";
constexpr auto PREFS_URL PROGMEM = "/prefs";
constexpr auto EGPREFS_URL PROGMEM = "/param";
#ifdef ESPGEIGER_HW
constexpr auto HV_URL PROGMEM = "/hv";
constexpr auto HV_JS_URL PROGMEM = "/hvjs";
constexpr auto HV_JSON_URL PROGMEM = "/hvjson";
constexpr auto HV_SET_URL PROGMEM = "/hvset";
#endif
#if GEIGER_IS_TEST(GEIGER_TYPE)
constexpr auto SETCPM_URL PROGMEM = "/cpm";
#endif

const char HTTP_HEAD_CTJS[17] PROGMEM = "text/javascript";
const char HTTP_HEAD_CTJSON[18] PROGMEM = "application/json";

#ifndef MQTT_DISCOVERY_TOPIC
#  define MQTT_DISCOVERY_TOPIC "homeassistant"
#endif


#ifndef MQTT_HASS_DEFAULT
#if GEIGER_IS_TEST(GEIGER_TYPE)
#  define MQTT_HASS_DEFAULT "0"
#else
#  define MQTT_HASS_DEFAULT "1"
#endif
#endif

#define MQTT_DEFAULT_PORT "8883"
//char mqttServer[MQTT_SERVER_LENGTH] = "";
//char mqttPort[MQTT_PORT_LENGTH] = MQTT_DEFAULT_PORT;
//char mqttUser[MQTT_USER_LENGTH] = "";
//char mqttPass[MQTT_PASS_LENGTH] = "";

// Override either via -D CMAN_PROCESS_INTERVAL_MS=N.
#ifndef CMAN_PROCESS_INTERVAL_MS
#ifdef ESP32
#define CMAN_PROCESS_INTERVAL_MS 10
#else
#define CMAN_PROCESS_INTERVAL_MS 5
#endif
#endif

class ConfigManager : public WiFiManager
{
public:
  static ConfigManager &getInstance()
  {
    static ConfigManager instance;
    return instance;
  }
  void bindServerCallback();
  bool autoConnect();
  void startWebPortal();
  bool hasWiFiCreds();
  void preSaveParams();
  void delay(unsigned long m);
  // Self-throttled wrapper around the inherited process(). Call from loop()
  // every iteration; only forwards to WiFiManager::process() at most every
  // CMAN_PROCESS_INTERVAL_MS (platform-specific default). Prevents per-iter
  // lwIP socket yield on ESP32 and saves idle polling cost on ESP8266.
  void processLoop(unsigned long now);
  void eraseSettings();
  const char* getHostName() { return hostName; };
  const char* getChipID() { return chipId; };
  const char* getThingName() { return THING_NAME; };
  const char* getUserAgent() { return userAgent; };
  ParsedTime parseTime(const char* timeStr);
private:
  ConfigManager();
  void handleRoot();
  void handleJSReturn();
  void handleJsonReturn();
  void handleStatusPage();
#ifdef DEBUG_PREFS
  void handlePrefs();  // debug-only: /prefs?m=<module>&k=<key>
#endif
  void beginChunkedPage(const char* contentType = nullptr);
  void sendPageHead(const char* title);
  void endChunkedPage();
  void handleEGPrefs();  // GET renders form, POST saves + re-renders
  void handleHistoryPage();
  void handleRefreshConsole();
  void handleRestart();
  void handleResetParams();
  void handleRegisterAPI();
  void handleNTP();
  void handleNTPSet();
  void handleClicksReturn();
  void handleGeigerLog();
#ifdef SERIALOUT
  void handleSerialOut();
#endif
  void handleAbout();
  void handleInfo();
  void handleOutputsJson();
#ifdef ESPGEIGER_HW
  void handleHVPage();
  void handleHVJSReturn();
  void handleHVJsonReturn();
  void handleHVSet();
#endif
#if GEIGER_IS_TEST(GEIGER_TYPE)
  void handleSetCPM();
#endif
  char chipId[7] = "";
  char hostName[20] = "";
  char userAgent[80] = "";
  char macAddr[18] = "";
  char* portalMenuHtml = nullptr;
  static constexpr size_t PORTAL_MENU_HTML_SIZE = 192;
  void updatePortalMessage();
};

#endif
