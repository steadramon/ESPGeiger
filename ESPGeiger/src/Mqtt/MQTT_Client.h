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
#ifdef MQTTOUT
#define LOG_TAG "MQTT"

#include "../ConfigManager/ConfigManager.h"
#include "../Status.h"
#include "../Counter/Counter.h"
#include <AsyncMqttClient.h>
#include <WiFiClient.h>

#define MQTT_MAX_PACKET_SIZE 1024
#define MQTT_JSON_BUFFER_SIZE 1024
#define MQTT_MIN_TIME 5
#define MQTT_MAX_TIME 3600
#define MQTT_STATUS_INTERVAL 60000

constexpr auto MQTT_LWT_ONLINE PROGMEM = "Online";
constexpr auto MQTT_LWT_OFFLINE PROGMEM = "Offline";

const char MQTT_TELE_TOPIC[18] PROGMEM = "%st%/tele/%cm%";
const char MQTT_STAT_TOPIC[18] PROGMEM = "%st%/stat/%cm%";

constexpr auto MQTT_TOPIC_LWT PROGMEM = "lwt";
constexpr auto MQTT_TOPIC_STATUS PROGMEM = "status";

extern Status status;
extern Counter gcounter;

struct MQTTMessage {
  String topic;
  String payload;
  uint8_t qos;  ///< QoS. Only for last will testaments.
  bool retain;
};

class MQTT_Client {
public:
  static MQTT_Client& getInstance()
  {
    static MQTT_Client instance; 
    return instance;
  }
  void begin();
  void loop(unsigned long now);
  void setInterval(int interval);
  int getInterval();
  void sendStatus();
  void disconnect();
  void onMqttConnect(bool sessionPresent);
  void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
#ifdef MQTTAUTODISCOVER
  void removeHASSConfig();
#endif
  MQTTMessage last_will_;
protected:
  void reconnect();

private:
  MQTT_Client();
  static ConfigManager _configManager;
  String buildTopic(const char * baseTopic, const char * cmd);
#ifdef MQTTAUTODISCOVER
  void setupHassAuto();
  void setupHassCB();
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
#endif
  void onMqttMessage(char* topic, char* payload);
  unsigned long lastPing = 0;
  unsigned long lastStatus = 0;
  unsigned long lastConnectionAtempt = 0;
  bool mqttEnabled = true;

  uint16_t statusInterval = MQTT_STATUS_INTERVAL;
  uint16_t pingInterval = 1 * 60 * 1000;
  const unsigned long reconnectionInterval = 60 * 1000;

  const char* teleTopic = MQTT_TELE_TOPIC;
  const char* statTopic = MQTT_STAT_TOPIC;

  // tele
  const char* topicStatus = MQTT_TOPIC_STATUS;
  const char* topicLWT = MQTT_TOPIC_LWT;

  const char* lwtOnline = MQTT_LWT_ONLINE;
  const char* lwtOffline = MQTT_LWT_OFFLINE;
  bool warnSent;
  bool alarmSent;
};

#endif
#endif
