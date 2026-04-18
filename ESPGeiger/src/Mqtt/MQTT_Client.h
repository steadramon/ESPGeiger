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

#include "../Status.h"
#include "../Util/DeviceInfo.h"
#include "../Counter/Counter.h"
#include "../Module/EGModule.h"
#include "../Prefs/EGPrefs.h"
#include <AsyncMqttClient.h>
#include <WiFiClient.h>


#ifndef MQTT_DISCOVERY_TOPIC
#define MQTT_DISCOVERY_TOPIC "homeassistant"
#endif

#ifndef MQTT_HASS_DEFAULT
#if GEIGER_IS_TEST(GEIGER_TYPE)
#define MQTT_HASS_DEFAULT "0"
#else
#define MQTT_HASS_DEFAULT "1"
#endif
#endif

#define MQTT_MAX_PACKET_SIZE 1024
#define MQTT_JSON_BUFFER_SIZE 1024
#define MQTT_MIN_TIME 5
#define MQTT_MAX_TIME 3600
#define MQTT_STATUS_INTERVAL 60

#ifndef MQTT_HASS_REFRESH_S
#define MQTT_HASS_REFRESH_S 3600UL
#endif

constexpr auto MQTT_LWT_ONLINE PROGMEM = "Online";
constexpr auto MQTT_LWT_OFFLINE PROGMEM = "Offline";

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
  bool has_loop() override { return true; }
  uint16_t loop_interval_ms() override { return 500; }
  uint16_t warmup_seconds() override { return 0; }
  void begin() override;
  void s_tick(unsigned long now) override;
  void loop(unsigned long now) override;
  const EGPrefGroup* prefs_group() override;
  void on_prefs_saved() override;  // reconnects with new settings
  uint8_t display_order() override { return 20; }
  size_t status_json(char* buf, size_t cap, unsigned long now) override;
  const EGLegacyAlias* legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
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
  bool connected = false;
  unsigned long last_attempt_ms = 0;
protected:
  void reconnect();

private:
  MQTT_Client();
  void buildTopic(char* out, size_t outsz, const char* middle, const char* cmd);
  char _cachedRootTopic[64] = "";
  bool _rootTopicCached = false;
#ifdef MQTTAUTODISCOVER
  void setupHassAuto();
  void setupHassCB();
  struct HassExtra { const char* key; const char* value; };
  void publishHassTopic(const char* type, const char* id, const char* displayName,
                        const char* devCla, const char* stateCla, const char* entCat,
                        const char* cmdTopic,
                        const HassExtra* extras, size_t n_extras);
  void removeHassTopic(const char* type, const char* id);
#endif
  void onMqttMessage(char* topic, char* payload);
  // s_tick raises bits in _pending; loop() drains one per call.
  void publishStatus();
  void publishPing();
  void publishWarn();
  void publishAlert();
  static constexpr uint8_t PEND_STATUS = 1 << 0;
  static constexpr uint8_t PEND_PING   = 1 << 1;
  static constexpr uint8_t PEND_WARN   = 1 << 2;
  static constexpr uint8_t PEND_ALERT  = 1 << 3;
  uint8_t _pending = 0;
  unsigned long lastPing = 0;
  unsigned long lastStatus = 0;
  unsigned long lastConnectionAttempt = 0;
  bool mqttEnabled = true;
  uint8_t reconnectAttempts = 0;

  uint16_t statusInterval = MQTT_STATUS_INTERVAL;
  uint16_t pingInterval = 60;
#ifdef MQTTAUTODISCOVER
  unsigned long _hass_last_publish = 0;
#endif

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
