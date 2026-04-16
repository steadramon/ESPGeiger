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

// How often (in seconds) to republish Home Assistant autodiscovery configs
// on reconnect. Default 1 hour — frequent enough to recover from broker
// restarts, rare enough to avoid flooding on flap-prone clients. Override
// with -D MQTT_HASS_REFRESH_S=N (set to 0 to publish on every reconnect).
#ifndef MQTT_HASS_REFRESH_S
#define MQTT_HASS_REFRESH_S 3600UL
#endif

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
  bool last_ok = false;
  unsigned long last_attempt_ms = 0;
protected:
  void reconnect();

private:
  MQTT_Client();
  static ConfigManager _configManager;
  String buildTopic(const char * baseTopic, const char * cmd);
  String _cachedRootTopic;
  bool _rootTopicCached = false;
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
  uint8_t reconnectAttempts = 0;

  unsigned long statusInterval = MQTT_STATUS_INTERVAL;
  unsigned long pingInterval = 1 * 60 * 1000;
#ifdef MQTTAUTODISCOVER
  unsigned long _hass_last_publish = 0;   // millis() of last HA autodiscovery publish (0 = never)
#endif

  const char* teleTopic = MQTT_TELE_TOPIC;
  const char* statTopic = MQTT_STAT_TOPIC;

  // tele
  const char* topicStatus = MQTT_TOPIC_STATUS;
  const char* topicLWT = MQTT_TOPIC_LWT;

  const char* lwtOnline = MQTT_LWT_ONLINE;
  const char* lwtOffline = MQTT_LWT_OFFLINE;
  bool warnSent = false;
  bool alertSent = false;
};

#endif
#endif
