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
#include "../Module/EGModule.h"
#include <AsyncMqttClient.h>
#include <WiFiClient.h>

#define MQTT_MAX_PACKET_SIZE 1024
#define MQTT_JSON_BUFFER_SIZE 1024
#define MQTT_MIN_TIME 5
#define MQTT_MAX_TIME 3600
#define MQTT_STATUS_INTERVAL 60000

#ifndef MQTT_HASS_REFRESH_S
#define MQTT_HASS_REFRESH_S 3600UL
#endif

constexpr auto MQTT_LWT_ONLINE PROGMEM = "Online";
constexpr auto MQTT_LWT_OFFLINE PROGMEM = "Offline";

constexpr auto MQTT_TOPIC_LWT PROGMEM = "lwt";
constexpr auto MQTT_TOPIC_STATUS PROGMEM = "status";

// Topics are built as "<root>/<middle>/<cmd>" directly from
// buildTopic(out, sz, middle, cmd). No runtime template parsing.

extern Status status;
extern Counter gcounter;

struct MQTTMessage {
  String topic;
  String payload;
  uint8_t qos;  ///< QoS. Only for last will testaments.
  bool retain;
};

class MQTT_Client : public EGModule {
public:
  static MQTT_Client& getInstance()
  {
    static MQTT_Client instance;
    return instance;
  }
  const char* name() override { return "mqtt"; }
  bool requires_wifi() override { return true; }
  bool has_tick() override { return true; }
  uint16_t warmup_seconds() override { return 0; }
  void begin() override;
  void s_tick(unsigned long now) override;
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
  void buildTopic(char* out, size_t outsz, const char* middle, const char* cmd);
  char _cachedRootTopic[64] = "";
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
                        std::vector<std::pair<const char*, const char*>> additionalEntries = {}
                      );
  void removeHassTopic(const String& mqttDeviceType, const String& mattDeviceName);
#endif
  void onMqttMessage(char* topic, char* payload);
  unsigned long lastPing = 0;
  unsigned long lastStatus = 0;
  unsigned long lastConnectionAttempt = 0;
  bool mqttEnabled = true;
  uint8_t reconnectAttempts = 0;

  unsigned long statusInterval = MQTT_STATUS_INTERVAL;
  unsigned long pingInterval = 1 * 60 * 1000;
#ifdef MQTTAUTODISCOVER
  unsigned long _hass_last_publish = 0;   // millis() of last HA autodiscovery publish (0 = never)
#endif

  // tele / stat middle paths are passed directly to buildTopic as literals.
  const char* topicStatus = MQTT_TOPIC_STATUS;
  const char* topicLWT = MQTT_TOPIC_LWT;

  const char* lwtOnline = MQTT_LWT_ONLINE;
  const char* lwtOffline = MQTT_LWT_OFFLINE;
  bool warnSent = false;
  bool alertSent = false;
};

extern MQTT_Client& mqtt;

#endif
#endif
