/*
  MQTT_Client.cpp - MQTT connection class

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
#ifdef MQTTOUT
#include "MQTT_Client.h"
#include "Counter/Counter.h"
#include "Counter/Tube.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Registry.h"
#include "Corium/Net/SendSlot.h"
#include "Corium/Measure/Measure.h"
#include "Counter/CounterMeasures.h"
#include "App/AppKeys.h"
#include "App/FeatureBits.h"
#include "App/MeasureIds.h"
#include "Corium/System/Clock.h"
#include "Corium/Net/Wifi.h"
#include "Corium/System/TickProfile.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/Util/Words.h"
#include <EGJson.h>
#include <EGMqtt.h>

CORIUM_PSTR(K_TOPIC,        "topic");
CORIUM_PSTR(K_HASS_ENABLED, "hass_enabled");
CORIUM_PSTR(K_HASS_TOPIC,   "hass_topic");

static const char T_STAT[] PROGMEM = "stat";
static const char T_TELE[] PROGMEM = "tele";
static const char LWT_ONLINE[] PROGMEM  = "Online";
static const char LWT_OFFLINE[] PROGMEM = "Offline";


static EGMqtt s_mq;

MQTT_Client& mqtt = MQTT_Client::getInstance();
CORIUM_REGISTER_FEATURE(mqtt, FEAT_MQTT)

#ifdef ESP8266
#define MQTT_PUB_YIELD() optimistic_yield(10000)
#else
#define MQTT_PUB_YIELD() yield()
#endif

// most each comes out at, cut beyond
static const size_t STATUS_MAX = 512;
static const size_t SENSOR_MAX = 384;

// into the outbox, nullptr = no room for max
static char* json_begin(const char* topic, size_t max, size_t* cap) {
  size_t room;
  char* p = (char*)s_mq.publish_begin(topic, 1, false, &room);
  if (!p || room < max) return nullptr;
  *cap = max;
  return p;
}

CORIUM_PSTR(MQ_L_SRV, "Server");
CORIUM_PSTR(MQ_H_SRV, "Broker address or IP");
CORIUM_PSTR(MQ_L_USR, "User");
CORIUM_PSTR(MQ_L_TPC, "Root Topic");
CORIUM_PSTR(MQ_H_TPC, "Root topic; {id}=chip ID");
CORIUM_PSTR(MQ_P_TPC, "[A-Za-z0-9_\\/\\{\\}\\-]+");
CORIUM_PSTR(MQ_H_INT, "Publish interval (sec)");
#ifdef MQTTAUTODISCOVER
CORIUM_PSTR(MQ_L_HEN, "HA Autodiscovery");
CORIUM_PSTR(MQ_H_HEN, "Publish HA autodiscovery");
CORIUM_PSTR(MQ_L_HTP, "HA Autodiscovery Topic");
CORIUM_PSTR(MQ_H_HTP, "HA discovery prefix");
CORIUM_PSTR(MQ_P_HTP, "[A-Za-z0-9_\\/\\-]+");
#endif

static const corium::Pref MQTT_PREF_ITEMS[] PROGMEM = {
  {corium::K_SERVER,    MQ_L_SRV, MQ_H_SRV, "",               corium::P_HOST, 0, 0, CORIUM_HOST_MAX, corium::P_STRING, 0},
  {EG_K_PORT,      EG_L_PRT, nullptr,  "1883",           nullptr,  1, 65535, 0,  corium::P_UINT,   0},
  {EG_K_USER,      MQ_L_USR, nullptr,  "",               nullptr,  0, 0,     64, corium::P_STRING, 0},
  {EG_K_PASSWORD,  EG_L_PWD, nullptr,  "",               nullptr,  0, 0,     64, corium::P_STRING, corium::P_SENSITIVE},
  {K_TOPIC,        MQ_L_TPC, MQ_H_TPC, "ESPGeiger-{id}", MQ_P_TPC, 0, 0, MQTT_TOPIC_PREF_MAX, corium::P_STRING, corium::P_ADVANCED},
  {EG_K_INTERVAL,  EG_L_INT, MQ_H_INT, STR(MQTT_INTERVAL), nullptr, MQTT_MIN_TIME, MQTT_MAX_TIME, 0, corium::P_UINT, 0},
#ifdef MQTTAUTODISCOVER
  {K_HASS_ENABLED, MQ_L_HEN, MQ_H_HEN, MQTT_HASS_DEFAULT,    nullptr,  0, 0, 0,  corium::P_BOOL,   corium::P_ADVANCED},
  {K_HASS_TOPIC,   MQ_L_HTP, MQ_H_HTP, MQTT_DISCOVERY_TOPIC, MQ_P_HTP, 0, 0, MQTT_HASS_TOPIC_MAX, corium::P_STRING, corium::P_ADVANCED},
#endif
};

static const corium::PrefGroup MQTT_PREF_GROUP PROGMEM = {
  "mqtt", "MQTT", 1,
  MQTT_PREF_ITEMS,
  sizeof(MQTT_PREF_ITEMS) / sizeof(MQTT_PREF_ITEMS[0]),
  corium::P_CAT_OUTPUT, corium::K_SERVER,
};

const corium::PrefGroup* MQTT_Client::prefs_group() { return &MQTT_PREF_GROUP; }

size_t MQTT_Client::status_json(char* buf, size_t cap, unsigned long now) {
  if (corium::Prefs::getString("mqtt", corium::K_SERVER)[0] == '\0') return 0;
  return write_status_json(buf, cap, "mqtt", last_ok && s_mq.connected(), last_attempt_ms, now);
}

// === LEGACY IMPORT (remove in v1.1.0) ===
static const corium::LegacyAlias MQTT_LEGACY[] PROGMEM = {
  {"mqttServer",   "server"},
  {"mqttPort",     "port"},
  {"mqttUser",     "user"},
  {"mqttPassword", "password"},
  {"mqttTopic",    "topic"},
  {"mqttTime",     "interval"},
#ifdef MQTTAUTODISCOVER
  {"hassSend",     "hass_enabled"},
  {"hassDisc",     "hass_topic"},
#endif
  {},
};
corium::LegacyRows MQTT_Client::legacy_aliases() { return MQTT_LEGACY; }
// === END LEGACY IMPORT ===

MQTT_Client::MQTT_Client()
{
}

void MQTT_Client::disconnect()
{
  s_mq.disconnect();
  mqttEnabled = true;
  reconnectAttempts = 0;
  lastConnectionAttempt = 0;
  _rootTopicCached = false;
#ifdef MQTTAUTODISCOVER
  _hass_next.clear();
#endif
}

void MQTT_Client::onMqttConnect(bool sessionPresent) {
  Log::console(PSTR("MQTT: Connected"));
  _reanchor = true;
  reconnectAttempts = 0;
  authFailures = 0;
  _pending |= PEND_LWT;
#ifdef MQTTAUTODISCOVER
  this->setupHassCB();
#endif
}

// one per EGMqtt::Error, 1-5 are the broker saying no
static const char* disconnect_reason_text(int8_t err) {
  switch (err) {
  case EGMqtt::TCP_FAILED:         return PSTR("TCP_FAILED");
  case EGMqtt::CONNECTION_LOST:    return PSTR("CONNECTION_LOST");
  case EGMqtt::TIMEOUT:            return PSTR("TIMEOUT");
  case EGMqtt::PROTOCOL:           return PSTR("PROTOCOL");
  case EGMqtt::NO_MEMORY:          return PSTR("NO_MEMORY");
  case EGMqtt::BAD_PROTOCOL:       return PSTR("BAD_PROTOCOL");
  case EGMqtt::ID_REJECTED:        return PSTR("ID_REJECTED");
  case EGMqtt::SERVER_UNAVAILABLE: return PSTR("SERVER_UNAVAILABLE");
  case EGMqtt::BAD_CREDENTIALS:    return PSTR("BAD_CREDENTIALS");
  case EGMqtt::NOT_AUTHORIZED:     return PSTR("NOT_AUTHORIZED");
  default:                         return PSTR("UNKNOWN");
  }
}

void MQTT_Client::onMqttDisconnect(int8_t err) {
  bool authFail = err == EGMqtt::BAD_CREDENTIALS || err == EGMqtt::NOT_AUTHORIZED;
  Log::console(PSTR("MQTT: Disconnected (%s)"), disconnect_reason_text(err));
  unsigned long now_ms = fast_millis();
  bool latched = false;
  if (authFail && ++authFailures >= 3) {
    mqttEnabled = false;
    s_mq.stop();   // memory back till the settings change
    latched = true;
  }
  lastConnectionAttempt = now_ms;
  wakeSoon();
  if (latched) Log::console(PSTR("MQTT: Credentials rejected 3 times, stopped until settings are saved"));
#ifdef MQTTAUTODISCOVER
  if (_hass_walk_state) _hass_idx = 0;
#endif
}

void MQTT_Client::setInterval(int interval) {
  pingInterval = clamp(interval, MQTT_MIN_TIME, MQTT_MAX_TIME);
}

int MQTT_Client::getInterval() {
  return (int)pingInterval;
}

unsigned long MQTT_Client::statusSlotAnchor(unsigned long now_s) {
  if (_slot_s < 0)
    _slot_s = (int16_t)(SendSlot::initial_offset(name(), statusInterval * 1000UL) / 1000UL);
  uint32_t off_s = (uint32_t)_slot_s;
  uint32_t until = off_s;
  if (Clock::synced) {
    uint32_t wall = Clock::epoch() % statusInterval;
    until = (off_s >= wall) ? (off_s - wall) : (statusInterval - wall + off_s);
  }
  return now_s + until - statusInterval;
}

#ifdef MQTTAUTODISCOVER
static uint32_t hass_spread_offset_s() {
  return DeviceInfo::chipHash() % (MQTT_HASS_BIRTH_SPREAD_S + 1);
}
#endif

#ifdef MQTTAUTODISCOVER
void MQTT_Client::scheduleHassDiscovery(bool reconnected)
{
  const unsigned long up_s = (unsigned long)DeviceInfo::uptime();
  if (reconnected && (!_hass_next.armed() || _hass_next.due(up_s))) {
    _hass_next.set(up_s + 2);
  }
  if (_hass_retrigger) {
    _hass_retrigger = false;
    _hass_next.set(up_s + hass_spread_offset_s());
  }
  if (_hass_next.due(up_s)) {
    triggerHassDiscovery();
  }
}
#endif

void MQTT_Client::queueTimedPublishes(unsigned long now)
{
  if ((now - lastStatus) > statusInterval) {
    lastStatus += statusInterval;
    if ((now - lastStatus) > statusInterval) lastStatus = now;
    _pending |= PEND_STATUS;
  }

  if ((now - lastPing) > pingInterval) {
    lastPing += pingInterval;
    if ((now - lastPing) > pingInterval) lastPing = now;
    _pending |= PEND_PING;
  }
}

void MQTT_Client::second(unsigned long now)
{
  if (!mqttEnabled) {
    return;
  }

  if (!s_mq.connected()) {
    _pending |= PEND_CONNECT;
    return;
  }

  const bool reconnected = _reanchor;
  if (reconnected) {
    _reanchor = false;
    // ping half a slot after status
    lastStatus = statusSlotAnchor(now);
    lastPing   = lastStatus + statusInterval + (statusInterval / 2) - pingInterval;
  }

#ifdef MQTTAUTODISCOVER
  scheduleHassDiscovery(reconnected);
#endif

  queueTimedPublishes(now);
}

// once warm, a full CPM bucket
void MQTT_Client::checkAlarms()
{
  if (!(_pending & PEND_ALARMS) || !corium::m_b(M_WARM)) return;
  _pending &= ~PEND_ALARMS;
  bool warn = corium::m_b(M_WARN);
  if (warn != warnSent) {
    warnSent = warn;
    _pending |= PEND_WARN;
  }
  bool alert = corium::m_b(M_ALERT);
  if (alert != alertSent) {
    alertSent = alert;
    _pending |= PEND_ALERT;
  }
}

void MQTT_Client::loop(unsigned long now)
{
  timers(now);   // before the client's news, as the tick was
  s_mq.loop();   // its callbacks run from here
  if (corium::Registry::events(this)) _pending |= PEND_ALARMS;
  if (!mqttEnabled) return;
  if (_pending & PEND_CONNECT) { _pending &= ~PEND_CONNECT; reconnect(); return; }
  if (!s_mq.connected()) return;
  checkAlarms();
  // one per call, hello then alarms; these stay till the outbox takes them
  if (sendHello()) return;
  if (_pending & PEND_WARN)       { if (publishWarn())  _pending &= ~PEND_WARN;  return; }
  if (_pending & PEND_ALERT)      { if (publishAlert()) _pending &= ~PEND_ALERT; return; }
  // no room for status, ping is smaller
  if ((_pending & PEND_STATUS) && publishStatus()) { _pending &= ~PEND_STATUS; return; }
  if (_pending & PEND_PING)       { _pending &= ~PEND_PING;   publishPing();   return; }
#ifdef MQTTAUTODISCOVER
  if (_hass_walk_state)           { stepHassDiscovery(); }
#endif
}

// Online, then the HA subscribe; true = this call used
bool MQTT_Client::sendHello()
{
  if (_pending & PEND_LWT) {
    char topic[MQTT_TOPIC_LEN];
    buildTopic(topic, sizeof(topic), T_TELE, PSTR("lwt"));
    if (s_mq.publish(topic, FPSTR(LWT_ONLINE), 1, true)) _pending &= ~PEND_LWT;
    return true;
  }
#ifdef MQTTAUTODISCOVER
  if (_pending & PEND_SUB) {
    if (s_mq.subscribe(_hass_status, 1)) _pending &= ~PEND_SUB;
    return true;
  }
#endif
  return false;
}

bool MQTT_Client::publishWarn()
{
  char topic[MQTT_TOPIC_LEN];
  buildTopic(topic, sizeof(topic), T_STAT, PSTR("WARN"));
  return s_mq.publish(topic, warnSent ? F("1") : F("0"), 1, false);
}

bool MQTT_Client::publishAlert()
{
  char topic[MQTT_TOPIC_LEN];
  buildTopic(topic, sizeof(topic), T_STAT, PSTR("ALERT"));
  return s_mq.publish(topic, alertSent ? F("1") : F("0"), 1, false);
}

bool MQTT_Client::publishStatus()
{
  char topic[MQTT_TOPIC_LEN];
  buildTopic(topic, sizeof(topic), T_TELE, PSTR("status"));
  size_t cap;
  char* out = json_begin(topic, STATUS_MAX, &cap);
  if (!out) {
    // counted once, then waits for room
    if (!_status_late) track(0);
    _status_late = true;
    return false;
  }
  _status_late = false;
  corium::Registry::emit(TOPIC_SENT);
  // pre-NTP time gives a bad year
  char dateTime[sizeof("2026-01-01T00:00:00Z")];
  dateTime[0] = '\0';
  struct tm t;
  if (Clock::utcTm(&t)) {
    snprintf_P(dateTime, sizeof(dateTime), PSTR("%04d-%02d-%02dT%02d:%02d:%02dZ"),
      1900 + t.tm_year, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  }

  char ipStr[CORIUM_IPV4_MAX + 1];
  Wifi::formatIP(ipStr, sizeof(ipStr));
  EGJson j(out, cap);
  j.map_begin();
  j.str(PSTR("time"), dateTime);
  j.uint(PSTR("ut"), DeviceInfo::uptime());
  j.str(PSTR("board"), DeviceInfo::board());
  j.str(PSTR("model"), Tube::model());
  j.str(PSTR("ssid"), Wifi::ssid);
  j.str(PSTR("ip"), ipStr);
  j.sint(PSTR("rssi"), Wifi::rssi);
  j.uint(PSTR("c_total"), corium::m_u(M_TC));
  j.uint(PSTR("tick"), TickProfile::tick_us);
  j.uint(PSTR("t_max"), TickProfile::tick_max_us);
  j.uint(PSTR("lps"), TickProfile::lps);
#ifdef MQTT_MEM_DEBUG
  #ifdef ESP8266
    uint32_t heap_free;
    uint16_t heap_max;
    uint8_t heap_frag;
    DeviceInfo::heapStats(&heap_free, &heap_max, &heap_frag);
    j.uint(PSTR("free_mem"), heap_free);
    j.uint(PSTR("heap_max"), heap_max);
    j.uint(PSTR("heap_frag"), heap_frag);
  #else
    j.uint(PSTR("free_mem"), DeviceInfo::freeHeap());
    j.uint(PSTR("min_free"), DeviceInfo::heapMin());
    j.uint(PSTR("heap_max"), DeviceInfo::heapMaxBlock());
  #endif
#else
  j.uint(PSTR("free_mem"), DeviceInfo::freeHeap());
#endif
#ifdef ESP32
  j.fixed(PSTR("cpu_temp"), DeviceInfo::chipTempC(), 1);
#endif
#if GEIGER_IS_SERIAL(GEIGER_TYPE) && !GEIGER_IS_TEST(GEIGER_TYPE)
  j.uint(PSTR("ser_ok"), corium::m_b(M_SER_OK) ? 1 : 0);
#endif
  j.map_end();
  if (j.overflow) Log::console(PSTR("MQTT: status cut to fit"));
  track(s_mq.publish_end(j.length()));
  Log::debug(PSTR("MQTT: Published"));
  return true;
}

// ok once the broker acks it, not when queued
void MQTT_Client::track(uint16_t pid)
{
  if (_ack_pid) note_result(false);   // the last one never came back
  _ack_pid = pid;
  if (pid) note_attempt();
  else note_publish(false);
}

void MQTT_Client::onMqttAck(uint16_t id)
{
  if (!_ack_pid || id != _ack_pid) return;
  _ack_pid = 0;
  note_result(true);
}

uint16_t MQTT_Client::publishSensor()
{
  char topic[MQTT_TOPIC_LEN];
  buildTopic(topic, sizeof(topic), T_TELE, PSTR("sensor"));
  size_t cap;
  char* out = json_begin(topic, SENSOR_MAX, &cap);
  if (!out) return 0;
  EGJson j(out, cap);
  j.map_begin();
  corium::m_json_fields(EGM_OUT_MQTT, j);
  j.map_end();
  uint16_t pid = s_mq.publish_end(j.length());
  MQTT_PUB_YIELD();
  return pid;
}

// ---- LEGACY ----
// stat/ topics
struct StatTopic { char id[6]; char topic[6]; };
static const StatTopic STAT_TOPICS[] PROGMEM = {
  {"cpm", "CPM"}, {"cpm5", "CPM5"}, {"cpm15", "CPM15"}, {"usv", "uSv"},
#ifndef DISABLE_MQTT_CPS
  {"cps", "CPS"},
#endif
  {"hv", "HV"}, {"t", "T"}, {"h", "H"}, {"p", "P"},
};

static const char* stat_topic(const char* id) {
  for (const StatTopic& s : STAT_TOPICS) {
    if (strcmp_P(id, s.id) == 0) return s.topic;
  }
  return nullptr;
}

void MQTT_Client::publishStat(const char* cmd, const char* text)
{
  char topic[MQTT_TOPIC_LEN];
  buildTopic(topic, sizeof(topic), T_STAT, cmd);
  s_mq.publish(topic, text, 1, false);
  MQTT_PUB_YIELD();
}
// ---- end LEGACY block ----

void MQTT_Client::publishPing()
{
  publishSensor();

  // ---- LEGACY ----
  for (corium::MeasureIter it(EGM_OUT_MQTT); it.next(); ) {
    const char* topic = stat_topic(it.row().id);
    char text[24];
    if (!topic || corium::m_format(it.row(), it.value(), text, sizeof(text)) == 0) continue;
    publishStat(topic, text);
  }
  // ---- end LEGACY block ----

  corium::Registry::emit(TOPIC_PING);
}

// a stalled connect times out in EGMqtt
bool MQTT_Client::clientBusy()
{
  return s_mq.connected() || s_mq.state() == EGMqtt::CONNECTING;
}

static unsigned long reconnect_backoff_ms(uint8_t attempts)
{
  unsigned long backoff = 10000UL << min(attempts, (uint8_t)5);
  if (backoff > 300000UL) backoff = 300000UL;
  backoff += (DeviceInfo::chipHash() % 13u) * 1000u;   // 0..12 s
  return backoff;
}

// all from s_mq.loop()
static void mq_up(void* arg, bool session) {
  static_cast<MQTT_Client*>(arg)->onMqttConnect(session);
}

static void mq_down(void* arg, int8_t err) {
  static_cast<MQTT_Client*>(arg)->onMqttDisconnect(err);
}

static void mq_message(void* arg, const EGMqtt::Message& m) {
  static_cast<MQTT_Client*>(arg)->onMqttMessage(m.topic, (const char*)m.payload, m.len);
}

static void mq_ack(void* arg, const EGMqtt::Ack& a) {
  if (a.of == EGMqtt::ACK_PUBLISH) static_cast<MQTT_Client*>(arg)->onMqttAck(a.id);
}

void MQTT_Client::applyIntervalPref()
{
  int mqtt_time = (int)corium::Prefs::getUInt("mqtt", EG_K_INTERVAL);
  if (mqtt_time <= 0) mqtt_time = MQTT_INTERVAL;
  if (mqtt_time != getInterval()) {
    setInterval(mqtt_time);
    Log::console(PSTR("MQTT: Submission Interval %d seconds"), getInterval());
  }
}

static bool in_backoff(unsigned long last, unsigned long now, uint8_t attempts)
{
  unsigned long backoff = reconnect_backoff_ms(attempts);
  return last && now - last < backoff;
}

// as queueTimedPublishes: fires once now - last > interval
static uint32_t left_s(unsigned long last, uint16_t interval, unsigned long now)
{
  if ((now - last) > interval) return 1;
  return (uint32_t)(last + interval + 1 - now);
}

// the tick's old work, only when a timer is due
void MQTT_Client::timers(unsigned long now)
{
  if (!reached_max(now, _due_ms, DUE_MAX_MS)) return;
  // uptime, ms / 1000 wraps at 49.7 d
  unsigned long up_s = (unsigned long)DeviceInfo::uptime();
  second(up_s);
  _due_ms = (now / 1000 + nextDue(now, up_s)) * 1000;
}

// seconds to the first timer that can fire, at least 1
uint32_t MQTT_Client::nextDue(unsigned long now_ms, unsigned long now)
{
  if (!mqttEnabled) return 3600;   // begin() wakes it
  if (!s_mq.connected()) {
    // each second while connecting, as the tick was
    if (!lastConnectionAttempt || s_mq.state() == EGMqtt::CONNECTING) return 1;
    uint32_t at = lastConnectionAttempt + reconnect_backoff_ms(reconnectAttempts);
    long s = (long)((at + 999) / 1000 - now_ms / 1000);
    return s > 0 ? (uint32_t)s : 1;
  }
  if (_reanchor) return 1;
  uint32_t s = left_s(lastStatus, statusInterval, now);
  uint32_t p = left_s(lastPing, pingInterval, now);
  if (p < s) s = p;
#ifdef MQTTAUTODISCOVER
  if (_hass_retrigger) return 1;
  if (_hass_next.armed()) {
    long h = (long)(_hass_next.at - now);
    if (h < (long)s) s = h > 0 ? (uint32_t)h : 1;
  }
#endif
  return s;
}

// a callback moved a timer, look on the next second
void MQTT_Client::wakeSoon()
{
  _due_ms = (fast_millis() / 1000 + 1) * 1000;
}

static int mqtt_port()
{
  int port = (int)corium::Prefs::getUInt("mqtt", EG_K_PORT);
  return port > 0 ? port : 1883;
}

// no password without a user in 3.1.1
static bool set_credentials()
{
  const char* user = corium::Prefs::getString("mqtt", EG_K_USER);
  const char* pass = corium::Prefs::getString("mqtt", EG_K_PASSWORD);
  if (!user[0]) return s_mq.setCredentials(nullptr);
  return s_mq.setCredentials(user, pass[0] ? pass : nullptr);
}

void MQTT_Client::reconnect()
{
  if (clientBusy()) return;

  unsigned long now = fast_millis();
  if (in_backoff(lastConnectionAttempt, now, (uint8_t)reconnectAttempts)) return;
  lastConnectionAttempt = now;

  const char* _mqtt_server = corium::Prefs::getString("mqtt", corium::K_SERVER);
  if (_mqtt_server[0] == '\0') {
    mqttEnabled = false;
    s_mq.stop();
    return;
  }
  int port = mqtt_port();

  // every time, picks up a topic change
  char lwt[MQTT_TOPIC_LEN];
  buildTopic(lwt, sizeof(lwt), T_TELE, PSTR("lwt"));
  bool set = s_mq.setServer(_mqtt_server, (uint16_t)port) &&
             s_mq.setClientId(DeviceInfo::hostname()) &&
             s_mq.setWill(lwt, FPSTR(LWT_OFFLINE), 1, true) &&
             set_credentials();

  applyIntervalPref();
  if (reconnectAttempts < 10) reconnectAttempts++;

  if (!set || !s_mq.connect()) {
    Log::console(PSTR("MQTT: Connect refused by client (attempt %d)"), reconnectAttempts);
    return;
  }
  Log::console(PSTR("MQTT: Connecting (attempt %d) ... %s:%d"), reconnectAttempts, _mqtt_server, port);
}

const char* MQTT_Client::getRootTopic()
{
  if (!_rootTopicCached) {
    const char* rootTopic = corium::Prefs::getString("mqtt", K_TOPIC);
    const char* chipId = DeviceInfo::chipid();
    size_t w = 0;
    const size_t cap = sizeof(_cachedRootTopic) - 1;
    for (const char* p = rootTopic; *p && w < cap; ) {
      if (strncmp_P(p, PSTR("{id}"), 4) == 0) {
        size_t n = strlen(chipId);
        if (w + n > cap) n = cap - w;
        memcpy(_cachedRootTopic + w, chipId, n);
        w += n;
        p += 4;
      } else {
        _cachedRootTopic[w++] = *p++;
      }
    }
    while (w > 0 && _cachedRootTopic[w-1] == '/') w--;
    _cachedRootTopic[w] = '\0';
    _rootTopicCached = true;
  }
  return _cachedRootTopic;
}

void MQTT_Client::buildTopic(char* out, size_t outsz, const char* middle, const char* cmd)
{
  snprintf_P(out, outsz, PSTR("%s/%s/%s"), getRootTopic(), middle, cmd);
}
#ifdef MQTTAUTODISCOVER

// Topic suffix
static const char H_ST_SENSOR[] PROGMEM = "~/tele/sensor";
static const char H_ST_STATUS[] PROGMEM = "~/tele/status";
// state_class
static const char H_SC_MEAS[]   PROGMEM = "measurement";
static const char H_SC_TOTINC[] PROGMEM = "total_increasing";
// device_class
static const char H_DC_SIG[]    PROGMEM = "signal_strength";
static const char H_DC_DUR[]    PROGMEM = "duration";
static const char H_DC_PROB[]   PROGMEM = "problem";
static const char H_DC_SAFETY[] PROGMEM = "safety";
static const char H_DC_CONN[]   PROGMEM = "connectivity";
static const char H_DC_TEMP[]   PROGMEM = "temperature";
static const char H_DC_HUMID[]  PROGMEM = "humidity";
static const char H_DC_PRES[]   PROGMEM = "atmospheric_pressure";
// entity_category
static const char H_EC_DIAG[]   PROGMEM = "diagnostic";

enum { DC_NONE, DC_SIG, DC_DUR, DC_PROB, DC_SAFETY, DC_CONN, DC_TEMP, DC_HUMID, DC_PRES };
static const char* const H_DEV_CLA[] PROGMEM = {
  nullptr, H_DC_SIG, H_DC_DUR, H_DC_PROB, H_DC_SAFETY, H_DC_CONN, H_DC_TEMP, H_DC_HUMID, H_DC_PRES,
};

// row flags
static const uint8_t HR_STATUS = 1 << 0;   // tele/status, else tele/sensor
static const uint8_t HR_TOTAL  = 1 << 1;   // total_increasing, else measurement
static const uint8_t HR_DIAG   = 1 << 2;
static const uint8_t HR_BIN    = 1 << 3;   // binary_sensor
static const uint8_t HR_OPT    = 1 << 4;   // only with its measure row

struct HassRow {
  char id[9];
  char key[3];    // "" = id
  char name[17];
  char unit[7];
  char icon[23];  // after mdi:
  char prec[2];   // "" = HA default
  uint8_t dev;
  uint8_t flags;
};

// walk order
static const HassRow HASS_ROWS[] PROGMEM = {
  // tele/sensor
  {"cpm",      "",   "CPM",          "CPM",        "pulse",          "",  DC_NONE,  0},
  {"cpm5",     "",   "CPM5",         "CPM",        "pulse",          "",  DC_NONE,  0},
  {"cpm15",    "",   "CPM15",        "CPM",        "pulse",          "",  DC_NONE,  0},
  {"usv",      "",   "\u00B5Sv/h",   "\u00B5Sv/h", "radioactive",    "2", DC_NONE,  0},
  {"hv",       "",   "HV",           "V",          "lightning-bolt", "",  DC_NONE,  HR_OPT},
  {"temp",     "t",  "Temperature",  "°C",         "thermometer",    "",  DC_TEMP,  HR_OPT},
  {"humidity", "h",  "Humidity",     "%",          "water-percent",  "",  DC_HUMID, HR_OPT},
  {"pressure", "p",  "Pressure",     "hPa",        "gauge",          "",  DC_PRES,  HR_OPT},
  {"c_total",  "",   "Total Clicks", "",           "counter",        "",  DC_NONE,  HR_STATUS | HR_TOTAL},
  // tele/status
  {"tick",     "",   "tick",         "\u00B5s",    "timer-outline",  "",  DC_NONE,  HR_STATUS | HR_DIAG},
  {"t_max",    "",   "tick max",     "\u00B5s",    "timer-alert-outline", "", DC_NONE, HR_STATUS | HR_DIAG},
  {"lps",      "",   "LPS",          "1/s",        "speedometer",    "",  DC_NONE,  HR_STATUS | HR_DIAG},
  {"rssi",     "",   "RSSI",         "dBm",        "wifi",           "",  DC_SIG,   HR_STATUS | HR_DIAG},
  {"uptime",   "ut", "Uptime",       "s",          "clock-outline",  "",  DC_DUR,   HR_STATUS | HR_TOTAL | HR_DIAG},
  {"free_mem", "",   "Free Memory",  "B",          "memory",         "",  DC_NONE,  HR_STATUS | HR_DIAG},
#ifdef ESP32
  {"cpu_temp", "",   "CPU Temperature", "°C",      "chip",           "1", DC_TEMP,  HR_STATUS | HR_DIAG},
#endif
  // binary
  {"warn",     "",   "Warning",      "",           "alert-outline",  "",  DC_PROB,  HR_BIN},
  {"alert",    "",   "Alert",        "",           "alert-octagram-outline", "", DC_SAFETY, HR_BIN},
#if GEIGER_IS_SERIAL(GEIGER_TYPE) && !GEIGER_IS_TEST(GEIGER_TYPE)
  {"ser_ok",   "",   "Serial Connected", "",       "serial-port",    "",  DC_CONN,  HR_BIN | HR_STATUS},
#else
  {"tube",     "",   "Tube Active",  "",           "radioactive",    "",  DC_CONN,  HR_BIN},
#endif
};

// Discovery type
static const char H_TYPE_SENSOR[]  PROGMEM = "sensor";
static const char H_TYPE_BINSENS[] PROGMEM = "binary_sensor";

// row keys
static const char H_K_STAT[]  PROGMEM = "stat_t";
static const char H_K_VT[]    PROGMEM = "val_tpl";
static const char H_K_IC[]    PROGMEM = "ic";
static const char H_K_UOM[]   PROGMEM = "unit_of_meas";
static const char H_K_PREC[]  PROGMEM = "sug_dsp_prc";
static const char H_K_PLON[]  PROGMEM = "pl_on";
static const char H_K_PLOFF[] PROGMEM = "pl_off";
static const char H_V_ONE[]   PROGMEM = "1";
static const char H_V_ZERO[]  PROGMEM = "0";

static const char H_DISC_DEFAULT[] PROGMEM = MQTT_DISCOVERY_TOPIC;
static const char H_STATUS_FMT[]   PROGMEM = "%s/status";

static const size_t HASS_PATH_LEN = 128;
// payload, path, MQTT header and id
static const size_t HASS_PACKET_MAX = MQTT_JSON_BUFFER_SIZE + HASS_PATH_LEN + 16;

// read each use, a save frees it
static const char* hass_disc() {
  const char* d = corium::Prefs::getString("mqtt", K_HASS_TOPIC);
  return d[0] ? d : H_DISC_DEFAULT;
}

// id PROGMEM, absent or NaN = no entity
static bool has_row(const char* id) {
  corium::MeasureIter it(EGM_OUT_MQTT, false, id);
  return it.next();
}

static int buildHassPath(char* buf, size_t sz, const char* disc,
                          const char* type, const char* name) {
  return snprintf_P(buf, sz, PSTR("%s/%s/%s-%s/config"), disc, type, DeviceInfo::hostname(), name);
}

static const char* hass_key(const HassRow* r) {
  return pgm_read_byte(r->key) ? r->key : r->id;
}

static uint8_t hass_flags(const HassRow* r) {
  return pgm_read_byte(&r->flags);
}

// the idx-th row HA gets, nullptr past the end
static const HassRow* hass_row(uint8_t idx) {
  for (const HassRow& r : HASS_ROWS) {
    if ((hass_flags(&r) & HR_OPT) && !has_row(hass_key(&r))) continue;
    if (idx-- == 0) return &r;
  }
  return nullptr;
}

static const char* hass_type(const HassRow* r) {
  return (hass_flags(r) & HR_BIN) ? H_TYPE_BINSENS : H_TYPE_SENSOR;
}

void MQTT_Client::setupHassCB() {
  _hass_listen = false;
  _pending &= ~PEND_SUB;
  if (!corium::Prefs::getBool("mqtt", K_HASS_ENABLED)) return;
  const char* _discovery_topic = corium::Prefs::getString("mqtt", K_HASS_TOPIC);
  if (_discovery_topic[0] == '\0') return;
  // onMqttMessage compares this
  snprintf_P(_hass_status, sizeof(_hass_status), H_STATUS_FMT, _discovery_topic);
  _hass_listen = true;
  _pending |= PEND_SUB;
}

void MQTT_Client::setupHassAuto() {
  if (_hass_walk_state) return;
  if (!corium::Prefs::getBool("mqtt", K_HASS_ENABLED)) return;
  const char* _discovery_topic = corium::Prefs::getString("mqtt", K_HASS_TOPIC);
  if (_discovery_topic[0] == '\0') return;
  _hass_walk_state = 1;
  _hass_idx = 0;
  Log::console(PSTR("MQTT: Starting HA autodiscovery walk"));
}

void MQTT_Client::setupHassRemove() {
  if (_hass_walk_state) return;
  const char* _discovery_topic = corium::Prefs::getString("mqtt", K_HASS_TOPIC);
  if (_discovery_topic[0] == '\0') return;
  _hass_walk_state = -1;
  _hass_idx = 0;
  Log::console(PSTR("MQTT: Starting HA autodiscovery removal walk"));
}

void MQTT_Client::triggerHassDiscovery() {
  if (_hass_walk_state) return;
  unsigned long now_s = (unsigned long)DeviceInfo::uptime();
  if (fresh(now_s, _hass_last_publish, MQTT_HASS_MIN_INTERVAL_S)) {
    Log::debug(PSTR("MQTT: HA discovery throttled (%lu s since last)"),
               now_s - _hass_last_publish);
    return;
  }
  setupHassAuto();
  _hass_next.set(now_s + MQTT_HASS_REFRESH_S);
}

static void hass_device(EGJson& j, const char* host) {
  char ipStr[CORIUM_IPV4_MAX + 1];
  Wifi::formatIP(ipStr, sizeof(ipStr));
  j.map_begin(PSTR("device"));
  j.str(PSTR("mf"), PSTR(THING_NAME));
  j.str(PSTR("mdl"), DeviceInfo::board());
  j.str(PSTR("mdl_id"), PSTR(BUILD_ENV));
  j.str(PSTR("name"), host);
  j.str(PSTR("sw"), PSTR(RELEASE_VERSION "/" GIT_VERSION));
  j.str(PSTR("sn"), DeviceInfo::chipid());
  j.array_begin(PSTR("identifiers"));
  j.str(nullptr, host);
  j.array_end();
  j.array_begin(PSTR("connections"));
  j.array_begin();
  j.str(nullptr, PSTR("mac"));
  j.str(nullptr, DeviceInfo::mac());
  j.array_end();
  j.array_end();
  j.str_begin(PSTR("cu"));
  j.part(PSTR("http://"));
  j.part(ipStr);
  j.part(PSTR("/"));
  j.str_end();
  j.map_end();
}

static void hass_fields(EGJson& j, const HassRow* r) {
  uint8_t f = hass_flags(r);
  uint8_t dev = pgm_read_byte(&r->dev);
  if (dev) j.str(PSTR("dev_cla"), (const char*)pgm_read_ptr(&H_DEV_CLA[dev]));
  if (!(f & HR_BIN)) j.str(PSTR("stat_cla"), (f & HR_TOTAL) ? H_SC_TOTINC : H_SC_MEAS);
  if (f & HR_DIAG) j.str(PSTR("ent_cat"), H_EC_DIAG);
  j.str(H_K_STAT, (f & HR_STATUS) ? H_ST_STATUS : H_ST_SENSOR);
  j.str_begin(H_K_VT);
  j.part(PSTR("{{ value_json."));
  j.part(hass_key(r));
  // gone = unknown in HA
  if (f & HR_OPT) j.part(PSTR(" | default(None)"));
  j.part(PSTR(" }}"));
  j.str_end();
  if (f & HR_BIN) {
    j.str(H_K_PLON, H_V_ONE);
    j.str(H_K_PLOFF, H_V_ZERO);
  }
  j.str_begin(H_K_IC);
  j.part(PSTR("mdi:"));
  j.part(r->icon);
  j.str_end();
  if (pgm_read_byte(r->unit)) j.str(H_K_UOM, r->unit);
  if (pgm_read_byte(r->prec)) j.str(H_K_PREC, r->prec);
}

CORIUM_PSTR(HA_DROPPED, "MQTT: HA discovery publish dropped: %s");

static void hass_publish(const HassRow* r, const char* root)
{
  const char* host = DeviceInfo::hostname();
  char path[HASS_PATH_LEN];
  buildHassPath(path, sizeof(path), hass_disc(), hass_type(r), r->id);
  size_t cap;
  char* out = json_begin(path, MQTT_JSON_BUFFER_SIZE, &cap);
  if (!out) {
    Log::console(HA_DROPPED, r->id);
    return;
  }

  EGJson j(out, cap);
  j.map_begin();
  hass_device(j, host);
  j.str(PSTR("avty_t"), PSTR("~/tele/lwt"));
  j.str(PSTR("pl_avail"), LWT_ONLINE);
  j.str(PSTR("pl_not_avail"), LWT_OFFLINE);
  j.str(PSTR("~"), root);
  j.str_begin(PSTR("name"));
  j.part(host);
  j.part(PSTR(" "));
  j.part(r->name);
  j.str_end();
  j.str_begin(PSTR("uniq_id"));
  j.part(host);
  j.part(PSTR("_"));
  j.part(r->id);
  j.str_end();
  hass_fields(j, r);
  j.map_end();
  // partial is worse than none; not ended, nothing goes
  if (j.overflow) {
    Log::console(PSTR("MQTT: HA discovery too big, not sent: %s"), r->id);
    return;
  }
  if (s_mq.publish_end(j.length()) == 0) {
    Log::console(HA_DROPPED, r->id);
  }
  MQTT_PUB_YIELD();
}

static void hass_remove(const HassRow* r)
{
  char path[HASS_PATH_LEN];
  buildHassPath(path, sizeof(path), hass_disc(), hass_type(r), r->id);
  s_mq.publish(path, "", 1, false);
}

void MQTT_Client::stepHassDiscovery() {
  if (_hass_walk_state == 0) return;
  // wait for acks, a refused one would be a sensor missing in HA
  if (s_mq.room() < HASS_PACKET_MAX) return;
  const HassRow* r = hass_row(_hass_idx);
  if (r) {
    if (_hass_walk_state > 0) hass_publish(r, getRootTopic());
    else hass_remove(r);
    _hass_idx++;
  } else {
    if (_hass_walk_state > 0) {
      _hass_last_publish = (unsigned long)DeviceInfo::uptime();
      if (_hass_last_publish == 0) _hass_last_publish = 1;
    }
    _hass_walk_state = 0;
  }
}
#endif

void MQTT_Client::onMqttMessage(const char* topic, const char* payload, size_t len)
{
#ifdef MQTTAUTODISCOVER
  if (_hass_listen && strcmp(_hass_status, topic) == 0) {
    if (len == 6 && strncmp_P(payload, PSTR("online"), 6) == 0) {
      Log::debug(PSTR("MQTT: HA is back online"));
      _hass_retrigger = true;
      wakeSoon();
    }
  }
#else
  (void)topic; (void)payload; (void)len;
#endif
}

void MQTT_Client::begin()
{
  const char* _mqtt_server = corium::Prefs::getString("mqtt", corium::K_SERVER);
  if (_mqtt_server[0] == '\0') {
    mqttEnabled = false;
    s_mq.stop();
    corium::Registry::every(this, 0);
    return;
  }
  mqttEnabled = true;
  wakeSoon();
  s_mq.setKeepAlive(MQTT_KEEPALIVE_S);
  s_mq.setTimeout(MQTT_TIMEOUT_S);
  s_mq.onConnect(mq_up, this);
  s_mq.onDisconnect(mq_down, this);
  s_mq.onMessage(mq_message, this);
  s_mq.onAck(mq_ack, this);
  corium::Registry::every(this, LOOP_MS);
  corium::Registry::on(Counter::TOPIC_ALARM, this);
  _pending |= PEND_ALARMS;   // an alarm already up when turned on
}

void MQTT_Client::on_prefs_saved()
{
  _rootTopicCached = false;
  bool needs_hass_remove = false;
#ifdef MQTTAUTODISCOVER
  needs_hass_remove = (s_mq.connected() && !corium::Prefs::getBool("mqtt", K_HASS_ENABLED));
#endif
  disconnect();
  authFailures = 0;
#ifdef MQTTAUTODISCOVER
  // publish walk restarts on reconnect
  if (_hass_walk_state > 0) _hass_walk_state = 0;
  if (needs_hass_remove) setupHassRemove();
#endif
  begin();
}
#endif
