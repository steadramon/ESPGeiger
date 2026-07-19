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

#include "../Util/Globals.h"
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
#ifndef MQTT_JSON_BUFFER_SIZE
#define MQTT_JSON_BUFFER_SIZE 1024
#endif
#define MQTT_MIN_TIME 5
#define MQTT_MAX_TIME 3600
#define MQTT_STATUS_INTERVAL 60

#ifndef MQTT_HASS_REFRESH_S
#define MQTT_HASS_REFRESH_S 3600UL
#endif

#ifndef MQTT_HASS_MIN_INTERVAL_S
#define MQTT_HASS_MIN_INTERVAL_S 30UL
#endif

#ifndef MQTT_HASS_BIRTH_SPREAD_S
#define MQTT_HASS_BIRTH_SPREAD_S 30UL   // stagger a fleet's re-announce on HA birth
#endif

constexpr auto MQTT_LWT_ONLINE PROGMEM = "Online";
constexpr auto MQTT_LWT_OFFLINE PROGMEM = "Offline";

constexpr auto MQTT_TOPIC_LWT PROGMEM = "lwt";
constexpr auto MQTT_TOPIC_STATUS PROGMEM = "status";

extern Counter gcounter;

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
  uint8_t display_order() override { return 25; }
  size_t status_json(char* buf, size_t cap, unsigned long now) override;
  const EGLegacyAlias* legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
  void setInterval(int interval);
  int getInterval();
  void disconnect();
  void onMqttConnect(bool sessionPresent);
  void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
#ifdef MQTTAUTODISCOVER
#endif
  char _lwt_topic[64] = "";
  // Written from the AsyncMqttClient callbacks (AsyncTCP task on ESP32),
  // read from s_tick/loop: EG_XTASK_VOLATILE for cross-task visibility. RMW on
  // these must hold MQTT_CB_LOCK (see onMqttDisconnect / on_prefs_saved).
  EG_XTASK_VOLATILE bool connected = false;
protected:
  void reconnect();

private:
  MQTT_Client();
  void buildTopic(char* out, size_t outsz, const char* middle, const char* cmd);
  const char* getRootTopic();
  char _cachedRootTopic[64] = "";
  bool _rootTopicCached = false;
#ifdef MQTTAUTODISCOVER
  void setupHassAuto();
  void setupHassRemove();
  void triggerHassDiscovery();
  bool publishHassNext(uint8_t idx);
  bool removeHassNext(uint8_t idx);
  void stepHassDiscovery();
  void setupHassCB();
  struct HassExtra { const char* key; const char* value; };
  // Rows filled in by the forEach*Hass* walkers. Every member is
  // PGM_P - assembled from PSTR() literals + palette constants in
  // MQTT_Client.cpp.
  struct HassSensorRow {
    const char* id;
    const char* name;
    const char* val_tpl;
    const char* unit;
    const char* icon;
    const char* stat_t;
    const char* dev_cla;
    const char* state_cla;
    const char* ent_cat;
  };
  struct HassBinaryRow {
    const char* id;
    const char* name;
    const char* val_tpl;
    const char* icon;
    const char* stat_t;
    const char* dev_cla;
  };
  typedef void (MQTT_Client::*HassSensorFn)(const HassSensorRow&);
  typedef void (MQTT_Client::*HassBinaryFn)(const HassBinaryRow&);
  void hassPublishSensorAt(const HassSensorRow& r);
  void hassPublishBinaryAt(const HassBinaryRow& r);
  void hassRemoveSensorAt(const HassSensorRow& r);
  void hassRemoveBinaryAt(const HassBinaryRow& r);
  void forEachHassSensor(HassSensorFn fn);
  void forEachHassBinarySensor(HassBinaryFn fn);
  void hassPublishSensor(const HassSensorRow& r);
  void hassPublishBinarySensor(const HassBinaryRow& r);
  void hassRemoveSensor(const HassSensorRow& r);
  void hassRemoveBinarySensor(const HassBinaryRow& r);
  void publishHassTopic(const char* type, const char* id, const char* displayName,
                        const char* devCla, const char* stateCla, const char* entCat,
                        const char* cmdTopic,
                        const HassExtra* extras, size_t n_extras);
  void removeHassTopic(const char* type, const char* id);
#endif
  void onMqttMessage(char* topic, char* payload, size_t len);
  unsigned long statusSlotAnchor(unsigned long now_s);
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
  EG_XTASK_VOLATILE bool _reanchor = false;   // set on (re)connect; s_tick re-derives slot phase
  int16_t _slot_s = -1;     // claimed second-of-minute slot, computed once
  unsigned long lastPing = 0;
  unsigned long lastStatus = 0;
  EG_XTASK_VOLATILE unsigned long lastConnectionAttempt = 0;
  EG_XTASK_VOLATILE bool mqttEnabled = true;
  EG_XTASK_VOLATILE uint8_t reconnectAttempts = 0;
  EG_XTASK_VOLATILE uint8_t authFailures = 0;
#ifdef ESP32
  // Callbacks run on the AsyncTCP task; guards the few cross-task RMWs.
  portMUX_TYPE _cbMux = portMUX_INITIALIZER_UNLOCKED;
#endif

  uint16_t statusInterval = MQTT_STATUS_INTERVAL;
  uint16_t pingInterval = 60;
#ifdef MQTTAUTODISCOVER
  // onMqttMessage (AsyncTCP task) may not touch the walk state; it raises
  // this flag and s_tick runs triggerHassDiscovery in cooperative context.
  EG_XTASK_VOLATILE bool _hass_retrigger = false;
  unsigned long _hass_next_publish = 0;
  unsigned long _hass_last_publish = 0;
  int8_t  _hass_walk_state = 0;
  uint8_t _hass_idx = 0;
  uint8_t _hass_walk_cursor = 0;
  bool    _hass_walk_done = false;
  const char* _hass_active_disc = nullptr;
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
