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

#include "Corium/System/Globals.h"
#include "Corium/System/DeviceInfo.h"
#include "GeigerInput/GeigerInput.h"
#include "Corium/Module/EGModule.h"
#include "Corium/Prefs/EGPrefs.h"
#include <WiFiClient.h>


#ifndef MQTT_DISCOVERY_TOPIC
#define MQTT_DISCOVERY_TOPIC "homeassistant"
#endif
#define MQTT_HASS_TOPIC_MAX 32
#define MQTT_TOPIC_PREF_MAX 28
#define MQTT_ROOT_MAX  (MQTT_TOPIC_PREF_MAX * CHIPID_LEN / (sizeof("{id}") - 1))   // all {id}
#define MQTT_TOPIC_LEN (MQTT_ROOT_MAX + sizeof("/tele/status"))

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
#ifndef MQTT_INTERVAL
#define MQTT_INTERVAL 60
#endif
#define MQTT_STATUS_INTERVAL 60
#define MQTT_KEEPALIVE_S 15
#define MQTT_TIMEOUT_S 15   // connect, ping reply

#ifndef MQTT_HASS_REFRESH_S
#define MQTT_HASS_REFRESH_S 3600UL
#endif

#ifndef MQTT_HASS_MIN_INTERVAL_S
#define MQTT_HASS_MIN_INTERVAL_S 30UL
#endif

#ifndef MQTT_HASS_BIRTH_SPREAD_S
#define MQTT_HASS_BIRTH_SPREAD_S 30UL   // stagger a fleet on HA birth
#endif

class MQTT_Client : public EGModule {
public:
  static MQTT_Client& getInstance()
  {
    static MQTT_Client instance;
    return instance;
  }
  const char* name() override { return PSTR("mqtt"); }
  bool requires_wifi() override { return true; }
  bool has_tick() override { return true; }
  bool has_loop() override { return true; }
  uint16_t loop_interval_ms() override { return 500; }
  uint16_t warmup_seconds() override { return 0; }
  void begin() override;
  void s_tick(unsigned long now) override;
  void loop(unsigned long now) override;
  const EGPrefGroup* prefs_group() override;
  void on_prefs_saved() override;
  uint8_t display_order() override { return 25; }
  size_t status_json(char* buf, size_t cap, unsigned long now) override;
  EGLegacyRows legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
  void setInterval(int interval);
  int getInterval();
  void disconnect();
  void onMqttConnect(bool sessionPresent);
  void onMqttDisconnect(int8_t err);
  void onMqttMessage(const char* topic, const char* payload, size_t len);
  void onMqttAck(uint16_t id);
  char _lwt_topic[MQTT_ROOT_MAX + sizeof("/tele/lwt")] = "";
  bool connected = false;
protected:
  void reconnect();

private:
  MQTT_Client();
  bool clientBusy();
  void applyIntervalPref();
  void queueTimedPublishes(unsigned long now);
#ifdef MQTTAUTODISCOVER
  void scheduleHassDiscovery(bool reconnected);
#endif
  void buildTopic(char* out, size_t outsz, const char* middle, const char* cmd);
  const char* getRootTopic();
  char _cachedRootTopic[MQTT_ROOT_MAX + 1] = "";
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
  // all PGM_P
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
    const char* precision;  // "" = HA default
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
  struct HassTopic {
    const char* type;
    const char* id;
    const char* displayName;
    const char* devCla;
    const char* stateCla;
    const char* entCat;
    const char* cmdTopic;
  };
  void publishHassTopic(const HassTopic& t, const HassExtra* extras, size_t n_extras);
  void removeHassTopic(const char* type, const char* id);
#endif
  unsigned long statusSlotAnchor(unsigned long now_s);
  bool publishStatus();
  void publishPing();
  uint16_t publishSensor();
  void track(uint16_t pid);
  uint16_t _ack_pid = 0;   // the publish whose ack decides last_ok
  void publishStat(const char* cmd, const char* text);
  bool sendHello();
  bool publishWarn();
  bool publishAlert();
  static constexpr uint8_t PEND_STATUS = 1 << 0;
  static constexpr uint8_t PEND_PING   = 1 << 1;
  static constexpr uint8_t PEND_WARN   = 1 << 2;
  static constexpr uint8_t PEND_ALERT  = 1 << 3;
  static constexpr uint8_t PEND_CONNECT = 1 << 4;
  static constexpr uint8_t PEND_LWT    = 1 << 5;
  static constexpr uint8_t PEND_SUB    = 1 << 6;
  uint8_t _pending = 0;
  bool _status_late = false;   // refused, waiting for room
  bool _reanchor = false;
  int16_t _slot_s = -1;
  unsigned long lastPing = 0;
  unsigned long lastStatus = 0;
  unsigned long lastConnectionAttempt = 0;
  bool mqttEnabled = true;
  uint8_t reconnectAttempts = 0;
  uint8_t authFailures = 0;

  uint16_t statusInterval = MQTT_STATUS_INTERVAL;
  uint16_t pingInterval = MQTT_INTERVAL;
#ifdef MQTTAUTODISCOVER
  bool _hass_retrigger = false;   // HA came back, s_tick spreads it
  bool _hass_listen = false;
  char _hass_status[MQTT_HASS_TOPIC_MAX + sizeof("/status")] = "";
  unsigned long _hass_next_publish = 0;
  unsigned long _hass_last_publish = 0;
  int8_t  _hass_walk_state = 0;
  uint8_t _hass_idx = 0;
  uint8_t _hass_walk_cursor = 0;
  bool    _hass_walk_done = false;
#endif

  bool warnSent = false;
  bool alertSent = false;
};

extern MQTT_Client& mqtt;

#endif
#endif
