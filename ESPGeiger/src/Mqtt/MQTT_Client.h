/*
  MQTT_Client.h - MQTT connection class
  
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

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#define LOG_TAG "MQTT"

#include "../ConfigManager/ConfigManager.h"
#include "../Status.h"
#include "../Counter/Counter.h"
#include <PubSubClient.h>
#include <WiFiClient.h>

#define MQTT_MAX_PACKET_SIZE 1024
#define MQTT_JSON_BUFFER_SIZE 1024

constexpr auto MQTT_LWT_ONLINE PROGMEM = "Online";
constexpr auto MQTT_LWT_OFFLINE PROGMEM = "Offline";

const char MQTT_TELE_TOPIC[18] PROGMEM = "%st%/tele/%cm%";
const char MQTT_STAT_TOPIC[18] PROGMEM = "%st%/stat/%cm%";

constexpr auto MQTT_TOPIC_LWT PROGMEM = "lwt";
constexpr auto MQTT_TOPIC_STATUS PROGMEM = "status";

extern Status status;
extern Counter gcounter;

class MQTT_Client : public PubSubClient {
public:
  static MQTT_Client& getInstance()
  {
    static MQTT_Client instance; 
    return instance;
  }
  void begin();
  void loop();
  void sendStatus();
  void disconnect();
  void removeHASSConfig();
protected:
  WiFiClient espClient;
  void reconnect();

private:
  MQTT_Client();
  static ConfigManager _configManager;
  String buildTopic(const char * baseTopic, const char * cmnd);
  void publishHassTopic(const String& mqttDeviceType,
                        const String& mattDeviceName,
                        const String& displayName,
                        const String& name,
                        const String& stateTopic,
                        const String& deviceType,
                        const String& deviceClass,
                        const String& stateClass = "",
                        const String& entityCat = "",
                        const String& commandTopic = "",
                        std::vector<std::pair<char*, char*>> additionalEntries = {}
                      );
  void removeHassTopic(const String& mqttDeviceType, const String& mattDeviceName);
  unsigned long lastPing = 0;
  unsigned long lastConnectionAtempt = 0;
  uint8_t connectionAtempts = 0;
  bool mqttEnabled = true;

  const unsigned long pingInterval = 1 * 60 * 1000;
  const unsigned long reconnectionInterval = 15 * 1000;
  uint16_t connectionTimeout = 10 * 60 * 1000 / reconnectionInterval;

  const char* teleTopic = MQTT_TELE_TOPIC;
  const char* statTopic = MQTT_STAT_TOPIC;

  // tele
  const char* topicStatus = MQTT_TOPIC_STATUS;
  const char* topicLWT = MQTT_TOPIC_LWT;

  const char* lwtOnline = MQTT_LWT_ONLINE;
  const char* lwtOffline = MQTT_LWT_OFFLINE;
};

#endif