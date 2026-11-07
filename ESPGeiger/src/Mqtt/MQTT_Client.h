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

#include "App/AppConfig.h"
#include "Corium/System/DeviceInfo.h"
#include "GeigerInput/GeigerInput.h"
#include "Corium/Module/Module.h"
#include "Corium/Module/Registry.h"
#include "App/Topics.h"
#include "Corium/Prefs/Prefs.h"


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
// s, the interval pref's range
#define MQTT_MIN_TIME 5
#define MQTT_MAX_TIME 3600
#ifndef MQTT_INTERVAL
#define MQTT_INTERVAL 60
#endif
#define MQTT_STATUS_INTERVAL 60
#define MQTT_KEEPALIVE_S 15
#define MQTT_TIMEOUT_S 15   // connect, ping reply
// reconnect: doubles from 10 s to 5 min, plus 0-12 s per chip
#define MQTT_RETRY_MS     10000UL
#define MQTT_RETRY_MAX_MS 300000UL
#define MQTT_JITTER_S     12
#define MQTT_AUTH_TRIES   3

#ifndef MQTT_HASS_REFRESH_S
#define MQTT_HASS_REFRESH_S 3600UL
#endif

#ifndef MQTT_HASS_MIN_INTERVAL_S
#define MQTT_HASS_MIN_INTERVAL_S 30UL
#endif

#ifndef MQTT_HASS_BIRTH_SPREAD_S
#define MQTT_HASS_BIRTH_SPREAD_S 30UL   // stagger a fleet on HA birth
#endif

// tele/sensor and tele/status, stat/WARN and ALERT, HA discovery
class MQTT_Client : public corium::Sender {
public:
  static MQTT_Client& getInstance()
  {
    static MQTT_Client instance;
    return instance;
  }
  const char* name() override { return PSTR("mqtt"); }
  bool requiresWifi() override { return true; }
  static constexpr uint16_t LOOP_MS = 500;
  static constexpr uint8_t TOPIC_PING = Topic::PING;   // the send mark, no LED
  void begin() override;
  void loop(unsigned long now) override;
  const corium::PrefGroup* prefsGroup() override;
  void onPrefsSaved() override;
  uint8_t displayOrder() override { return 25; }
  size_t statusJson(char* buf, size_t cap, unsigned long now) override;
  corium::LegacyRows legacyAliases() override;  // LEGACY IMPORT (remove in v1.1.0)
  void setInterval(int interval);
  int getInterval();
  void disconnect();
  void onMqttConnect(bool sessionPresent);
  void onMqttDisconnect(int8_t err);
  void onMqttMessage(const char* topic, const char* payload, size_t len);
  void onMqttAck(uint16_t id);
protected:
  void reconnect();

private:
  MQTT_Client();
  void timers(unsigned long now);
  void second(unsigned long now_s);
  uint32_t nextDue(unsigned long now_ms, unsigned long now_s);
  void wakeSoon();
  uint32_t _due_ms = 0;   // timers next run
  static constexpr uint32_t DUE_MAX_MS = (MQTT_MAX_TIME + 1) * 1000UL;   // nextDue's most
  bool clientBusy();
  bool moved();
  void applyIntervalPref();
  void queueTimedPublishes(unsigned long now_s);
#ifdef MQTTAUTODISCOVER
  void scheduleHassDiscovery(bool reconnected);
#endif
  void buildTopic(char* out, size_t outsz, const char* middle, const char* cmd);
  const char* getRootTopic();
  char _root[MQTT_ROOT_MAX + 1] = {};
  bool _root_ok = false;
#ifdef MQTTAUTODISCOVER
  void setupHassAuto();
  void startHassWalk(int8_t dir);
  void triggerHassDiscovery();
  void stepHassDiscovery();
  void setupHassCB();
#endif
  unsigned long statusSlotAnchor(unsigned long now_s);
  bool publishStatus();
  void publishPing();
  uint16_t publishSensor();
  void track(uint16_t pid);
  uint16_t _ack_pid = 0;   // the publish whose ack decides lastOk
  void publishStat(const char* cmd, const char* text);
  bool sendHello();
  bool publishFlag(const char* cmd, bool on);   // cmd PROGMEM
  static constexpr uint8_t PEND_STATUS = 1 << 0;
  static constexpr uint8_t PEND_PING   = 1 << 1;
  static constexpr uint8_t PEND_WARN   = 1 << 2;
  static constexpr uint8_t PEND_ALERT  = 1 << 3;
  static constexpr uint8_t PEND_CONNECT = 1 << 4;
  static constexpr uint8_t PEND_LWT    = 1 << 5;
  static constexpr uint8_t PEND_SUB    = 1 << 6;
  static constexpr uint8_t PEND_ALARMS = 1 << 7;   // Counter's alarm changed, compare once warm
  uint8_t _pending = 0;
  void checkAlarms();
  bool _status_late = false;   // refused, waiting for room
  bool _reanchor = false;
  int16_t _slot_s = -1;
  unsigned long _ping_s = 0;
  unsigned long _status_s = 0;
  unsigned long _tried_ms = 0;
  bool _enabled = true;
  uint8_t _tries = 0;
  uint8_t _auth_fails = 0;

  uint16_t _status_every = MQTT_STATUS_INTERVAL;
  uint16_t _ping_every = MQTT_INTERVAL;
#ifdef MQTTAUTODISCOVER
  bool _hass_retrigger = false;   // HA came back, the timers spread it
  bool _hass_listen = false;
  char _hass_status[MQTT_HASS_TOPIC_MAX + sizeof("/status")] = {};
  corium::Deadline _hass_next;   // uptime s
  unsigned long _hass_last_s = 0;
  enum : int8_t { HASS_REMOVE = -1, HASS_IDLE, HASS_PUBLISH };
  int8_t  _hass_walk_state = HASS_IDLE;
  uint8_t _hass_idx = 0;
#endif

  // what the broker was last told
  bool _warn_sent = false;
  bool _alert_sent = false;
};

extern MQTT_Client& mqtt;

#endif
#endif
