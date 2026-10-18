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
#include "Corium/Output/LedSignal.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Measure/EGMeasure.h"
#include "Corium/System/Clock.h"
#include "Corium/Net/Wifi.h"
#include "Corium/System/TickProfile.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Util/Words.h"
#include <EGJson.h>
#include <EGMqtt.h>
#include "EnvSensor/EnvSensor.h"
#include <math.h>
#ifdef ESPG_HV_ADC
#include "HV/HV.h"
#endif

EG_PSTR(K_TOPIC,        "topic");
EG_PSTR(K_HASS_ENABLED, "hass_enabled");
EG_PSTR(K_HASS_TOPIC,   "hass_topic");

EG_PSTR(M_TC,     "tc");
EG_PSTR(M_SER_OK, "ser_ok");

static const char T_STAT[] PROGMEM = "stat";
static const char T_TELE[] PROGMEM = "tele";
static const char LWT_ONLINE[] PROGMEM  = "Online";
static const char LWT_OFFLINE[] PROGMEM = "Offline";


static EGMqtt s_mq;

MQTT_Client& mqtt = MQTT_Client::getInstance();
EG_REGISTER_MODULE(mqtt)

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

EG_PSTR(MQ_L_SRV, "Server");
EG_PSTR(MQ_H_SRV, "Broker address or IP");
EG_PSTR(MQ_L_USR, "User");
EG_PSTR(MQ_L_TPC, "Root Topic");
EG_PSTR(MQ_H_TPC, "Root topic; {id}=chip ID");
EG_PSTR(MQ_P_TPC, "[A-Za-z0-9_\\/\\{\\}\\-]+");
EG_PSTR(MQ_H_INT, "Publish interval (sec)");
#ifdef MQTTAUTODISCOVER
EG_PSTR(MQ_L_HEN, "HA Autodiscovery");
EG_PSTR(MQ_H_HEN, "Publish HA autodiscovery");
EG_PSTR(MQ_L_HTP, "HA Autodiscovery Topic");
EG_PSTR(MQ_H_HTP, "HA discovery prefix");
EG_PSTR(MQ_P_HTP, "[A-Za-z0-9_\\/\\-]+");
#endif

static const EGPref MQTT_PREF_ITEMS[] PROGMEM = {
  {EG_K_SERVER,    MQ_L_SRV, MQ_H_SRV, "",               EG_P_HOST, 0, 0, EG_HOST_MAX, EGP_STRING, 0},
  {EG_K_PORT,      EG_L_PRT, nullptr,  "1883",           nullptr,  1, 65535, 0,  EGP_UINT,   0},
  {EG_K_USER,      MQ_L_USR, nullptr,  "",               nullptr,  0, 0,     64, EGP_STRING, 0},
  {EG_K_PASSWORD,  EG_L_PWD, nullptr,  "",               nullptr,  0, 0,     64, EGP_STRING, EGP_SENSITIVE},
  {K_TOPIC,        MQ_L_TPC, MQ_H_TPC, "ESPGeiger-{id}", MQ_P_TPC, 0, 0, MQTT_TOPIC_PREF_MAX, EGP_STRING, EGP_ADVANCED},
  {EG_K_INTERVAL,  EG_L_INT, MQ_H_INT, STR(MQTT_INTERVAL), nullptr, MQTT_MIN_TIME, MQTT_MAX_TIME, 0, EGP_UINT, 0},
#ifdef MQTTAUTODISCOVER
  {K_HASS_ENABLED, MQ_L_HEN, MQ_H_HEN, MQTT_HASS_DEFAULT,    nullptr,  0, 0, 0,  EGP_BOOL,   EGP_ADVANCED},
  {K_HASS_TOPIC,   MQ_L_HTP, MQ_H_HTP, MQTT_DISCOVERY_TOPIC, MQ_P_HTP, 0, 0, MQTT_HASS_TOPIC_MAX, EGP_STRING, EGP_ADVANCED},
#endif
};

static const EGPrefGroup MQTT_PREF_GROUP PROGMEM = {
  "mqtt", "MQTT", 1,
  MQTT_PREF_ITEMS,
  sizeof(MQTT_PREF_ITEMS) / sizeof(MQTT_PREF_ITEMS[0]),
  EGP_CAT_OUTPUT, EG_K_SERVER,
};

const EGPrefGroup* MQTT_Client::prefs_group() { return &MQTT_PREF_GROUP; }

size_t MQTT_Client::status_json(char* buf, size_t cap, unsigned long now) {
  if (EGPrefs::getString("mqtt", EG_K_SERVER)[0] == '\0') return 0;
  return write_status_json(buf, cap, "mqtt", last_ok && connected, last_attempt_ms, now);
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias MQTT_LEGACY[] PROGMEM = {
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
EGLegacyRows MQTT_Client::legacy_aliases() { return MQTT_LEGACY; }
// === END LEGACY IMPORT ===

MQTT_Client::MQTT_Client()
{
}

void MQTT_Client::disconnect()
{
  s_mq.disconnect();
  connected = false;
  mqttEnabled = true;
  reconnectAttempts = 0;
  lastConnectionAttempt = 0;
  _rootTopicCached = false;
#ifdef MQTTAUTODISCOVER
  _hass_next_publish = 0;
#endif
}

void MQTT_Client::onMqttConnect(bool sessionPresent) {
  Log::console(PSTR("MQTT: Connected"));
  connected = true;
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
  connected = false;
  if (latched) Log::console(PSTR("MQTT: Credentials rejected 3 times, stopped until settings are saved"));
#ifdef MQTTAUTODISCOVER
  if (_hass_walk_state) _hass_idx = 0;
#endif
}

void MQTT_Client::setInterval(int interval) {
  if (interval < MQTT_MIN_TIME) {
    interval = MQTT_MIN_TIME;
  }
  if (interval > MQTT_MAX_TIME) {
    interval = MQTT_MAX_TIME;
  }
  pingInterval = interval;
}

int MQTT_Client::getInterval() {
  return (int)pingInterval;
}

unsigned long MQTT_Client::statusSlotAnchor(unsigned long now_s) {
  if (_slot_s < 0)
    _slot_s = (int16_t)(EGModuleRegistry::initial_offset(name(), statusInterval * 1000UL) / 1000UL);
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
  static uint32_t off = 0xFFFFFFFFu;
  if (off == 0xFFFFFFFFu) {
    uint32_t h = 2166136261u;
    for (const char* p = DeviceInfo::chipid(); p && *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    off = h % (MQTT_HASS_BIRTH_SPREAD_S + 1);
  }
  return off;
}
#endif

#ifdef MQTTAUTODISCOVER
void MQTT_Client::scheduleHassDiscovery(bool reconnected)
{
  const unsigned long up_s = (unsigned long)DeviceInfo::uptime();
  if (reconnected && (_hass_next_publish == 0 || (long)(up_s - _hass_next_publish) >= 0)) {
    _hass_next_publish = up_s + 2;
    if (_hass_next_publish == 0) _hass_next_publish = 1;
  }
  if (_hass_retrigger) {
    _hass_retrigger = false;
    _hass_next_publish = up_s + hass_spread_offset_s();
    if (_hass_next_publish == 0) _hass_next_publish = 1;
  }
  if (_hass_next_publish && (long)(up_s - _hass_next_publish) >= 0) {
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

void MQTT_Client::s_tick(unsigned long now)
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

  // wait for a full CPM bucket
  if (!gcounter.is_warm()) {
    return;
  }

  bool warn = gcounter.is_warning();
  if (warn != warnSent) {
    warnSent = warn;
    _pending |= PEND_WARN;
  }
  bool alert = gcounter.is_alert();
  if (alert != alertSent) {
    alertSent = alert;
    _pending |= PEND_ALERT;
  }
}

void MQTT_Client::loop(unsigned long /*now*/)
{
  s_mq.loop();   // its callbacks run from here
  if (!mqttEnabled) return;
  if (_pending & PEND_CONNECT) { _pending &= ~PEND_CONNECT; reconnect(); return; }
  if (!s_mq.connected()) return;
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
    if (s_mq.publish(_lwt_topic, FPSTR(LWT_ONLINE), 1, true)) _pending &= ~PEND_LWT;
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
  LedSignal::activity();
  send_indicator = 2;
  // pre-NTP time gives a bad year
  char dateTime[sizeof("2026-01-01T00:00:00Z")];
  dateTime[0] = '\0';
  struct tm t;
  if (Clock::utcTm(&t)) {
    snprintf_P(dateTime, sizeof(dateTime), PSTR("%04d-%02d-%02dT%02d:%02d:%02dZ"),
      1900 + t.tm_year, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  }

  char ipStr[EG_IPV4_MAX + 1];
  Wifi::formatIP(ipStr, sizeof(ipStr));
  EGJson j(out, cap);
  j.map_begin();
  j.str(PSTR("time"), dateTime);
  j.uint(PSTR("ut"), DeviceInfo::uptime());
  j.str(PSTR("board"), DeviceInfo::chipmodel());
  j.str(PSTR("model"), Tube::model());
  j.str(PSTR("ssid"), Wifi::ssid);
  j.str(PSTR("ip"), ipStr);
  j.sint(PSTR("rssi"), Wifi::rssi);
  j.uint(PSTR("c_total"), egm_u(M_TC));
  j.uint(PSTR("tick"), TickProfile::tick_us);
  j.uint(PSTR("t_max"), TickProfile::tick_max_us);
  j.uint(PSTR("lps"), TickProfile::lps);
#ifdef MQTT_MEM_DEBUG
  #ifdef ESP8266
    uint32_t heap_free;
    uint16_t heap_max;
    uint8_t heap_frag;
    ESP.getHeapStats(&heap_free, &heap_max, &heap_frag);
    j.uint(PSTR("free_mem"), heap_free);
    j.uint(PSTR("heap_max"), heap_max);
    j.uint(PSTR("heap_frag"), heap_frag);
  #else
    j.uint(PSTR("free_mem"), DeviceInfo::freeHeap());
    j.uint(PSTR("min_free"), ESP.getMinFreeHeap());
    j.uint(PSTR("heap_max"), ESP.getMaxAllocHeap());
  #endif
#else
  j.uint(PSTR("free_mem"), DeviceInfo::freeHeap());
#endif
#if GEIGER_IS_SERIAL(GEIGER_TYPE) && !GEIGER_IS_TEST(GEIGER_TYPE)
  j.uint(PSTR("ser_ok"), egm_b(M_SER_OK) ? 1 : 0);
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
  egm_json_fields(EGM_OUT_MQTT, j);
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
  for (EGMeasureIter it(EGM_OUT_MQTT); it.next(); ) {
    const char* topic = stat_topic(it.row().id);
    char text[24];
    if (!topic || egm_format(it.row(), it.value(), text, sizeof(text)) == 0) continue;
    publishStat(topic, text);
  }
  // ---- end LEGACY block ----

  send_indicator = 2;
}

// a stalled connect times out in EGMqtt
bool MQTT_Client::clientBusy()
{
  if (s_mq.connected()) {
    reconnectAttempts = 0;
    return true;
  }
  return s_mq.state() == EGMqtt::CONNECTING;
}

static unsigned long reconnect_backoff_ms(uint8_t attempts)
{
  unsigned long backoff = 10000UL << min(attempts, (uint8_t)5);
  if (backoff > 300000UL) backoff = 300000UL;
  static uint32_t s_jitter_ms = 0;
  if (s_jitter_ms == 0) {
    uint32_t h = 2166136261u;
    for (const char* p = DeviceInfo::chipid(); p && *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    s_jitter_ms = (h % 13u) * 1000u;   // 0..12 s
  }
  backoff += s_jitter_ms;
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
  int mqtt_time = (int)EGPrefs::getUInt("mqtt", EG_K_INTERVAL);
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

static int mqtt_port()
{
  int port = (int)EGPrefs::getUInt("mqtt", EG_K_PORT);
  return port > 0 ? port : 1883;
}

// no password without a user in 3.1.1
static bool set_credentials()
{
  const char* user = EGPrefs::getString("mqtt", EG_K_USER);
  const char* pass = EGPrefs::getString("mqtt", EG_K_PASSWORD);
  if (!user[0]) return s_mq.setCredentials(nullptr);
  return s_mq.setCredentials(user, pass[0] ? pass : nullptr);
}

void MQTT_Client::reconnect()
{
  if (clientBusy()) return;

  unsigned long now = fast_millis();
  if (in_backoff(lastConnectionAttempt, now, (uint8_t)reconnectAttempts)) return;
  lastConnectionAttempt = now;

  const char* _mqtt_server = EGPrefs::getString("mqtt", EG_K_SERVER);
  if (_mqtt_server[0] == '\0') {
    mqttEnabled = false;
    s_mq.stop();
    return;
  }
  int port = mqtt_port();

  // every time, picks up a topic change
  buildTopic(_lwt_topic, sizeof(_lwt_topic), T_TELE, PSTR("lwt"));
  bool set = s_mq.setServer(_mqtt_server, (uint16_t)port) &&
             s_mq.setClientId(DeviceInfo::hostname()) &&
             s_mq.setWill(_lwt_topic, FPSTR(LWT_OFFLINE), 1, true) &&
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
    const char* rootTopic = EGPrefs::getString("mqtt", K_TOPIC);
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
static const char H_EMPTY[]     PROGMEM = "";   // not set

// Discovery type
static const char H_TYPE_SENSOR[]  PROGMEM = "sensor";
static const char H_TYPE_BINSENS[] PROGMEM = "binary_sensor";

// HassExtra keys
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
  const char* d = EGPrefs::getString("mqtt", K_HASS_TOPIC);
  return d[0] ? d : H_DISC_DEFAULT;
}

static int buildHassPath(char* buf, size_t sz, const char* disc,
                          const char* type, const char* name) {
  return snprintf_P(buf, sz, PSTR("%s/%s/%s-%s/config"), disc, type, DeviceInfo::hostname(), name);
}

// PSTR needs function scope
void MQTT_Client::forEachHassSensor(HassSensorFn fn) {
  #define S(id_, jKey_, name_, unit_, icon_, stat_, dev_, state_, ent_, prec_) do { \
    if (_hass_walk_done) return; \
    HassSensorRow r = {PSTR(id_), PSTR(name_), PSTR("{{ value_json." jKey_ " }}"), \
                        PSTR(unit_), PSTR(icon_), stat_, dev_, state_, ent_, PSTR(prec_)}; \
    (this->*fn)(r); \
  } while (0)

  // tele/sensor
  S("cpm",      "cpm",      "CPM",          "CPM",       "mdi:pulse",          H_ST_SENSOR, H_EMPTY,  H_SC_MEAS,   H_EMPTY, "");
  S("cpm5",     "cpm5",     "CPM5",         "CPM",       "mdi:pulse",          H_ST_SENSOR, H_EMPTY,  H_SC_MEAS,   H_EMPTY, "");
  S("cpm15",    "cpm15",    "CPM15",        "CPM",       "mdi:pulse",          H_ST_SENSOR, H_EMPTY,  H_SC_MEAS,   H_EMPTY, "");
  S("usv",      "usv",      "\u00B5Sv/h",   "\u00B5Sv/h", "mdi:radioactive",    H_ST_SENSOR, H_EMPTY,  H_SC_MEAS,   H_EMPTY, "2");
#ifdef ESPG_HV_ADC
  if (hv.get_pwm_pin() >= 0 && hv.get_vd_ratio() != 0) {
    S("hv",     "hv",       "HV",           "V",         "mdi:lightning-bolt", H_ST_SENSOR, H_EMPTY,  H_SC_MEAS,   H_EMPTY, "");
  }
#endif
  if (envsensor.present()) {
    if (!isnan(envsensor.tempC())) {
      S("temp",     "t", "Temperature", "°C",  "mdi:thermometer",   H_ST_SENSOR, H_DC_TEMP,  H_SC_MEAS, H_EMPTY, "");
    }
    if (!isnan(envsensor.humidity())) {
      S("humidity", "h", "Humidity",    "%",   "mdi:water-percent", H_ST_SENSOR, H_DC_HUMID, H_SC_MEAS, H_EMPTY, "");
    }
    if (!isnan(envsensor.pressure())) {
      S("pressure", "p", "Pressure",    "hPa", "mdi:gauge",         H_ST_SENSOR, H_DC_PRES,  H_SC_MEAS, H_EMPTY, "");
    }
  }
  S("c_total",  "c_total",  "Total Clicks", "",          "mdi:counter",        H_ST_STATUS, H_EMPTY,  H_SC_TOTINC, H_EMPTY, "");
  // tele/status
  S("tick",     "tick",     "tick",         "\u00B5s",   "mdi:timer-outline",  H_ST_STATUS, H_EMPTY,  H_SC_MEAS,   H_EC_DIAG, "");
  S("t_max",    "t_max",    "tick max",     "\u00B5s",   "mdi:timer-alert-outline", H_ST_STATUS, H_EMPTY, H_SC_MEAS, H_EC_DIAG, "");
  S("lps",      "lps",      "LPS",          "1/s",       "mdi:speedometer",    H_ST_STATUS, H_EMPTY,  H_SC_MEAS,   H_EC_DIAG, "");
  S("rssi",     "rssi",     "RSSI",         "dBm",       "mdi:wifi",           H_ST_STATUS, H_DC_SIG, H_SC_MEAS,   H_EC_DIAG, "");
  S("uptime",   "ut",       "Uptime",       "s",         "mdi:clock-outline",  H_ST_STATUS, H_DC_DUR, H_SC_TOTINC, H_EC_DIAG, "");
  S("free_mem", "free_mem", "Free Memory",  "B",         "mdi:memory",         H_ST_STATUS, H_EMPTY,  H_SC_MEAS,   H_EC_DIAG, "");

  #undef S
}

void MQTT_Client::forEachHassBinarySensor(HassBinaryFn fn) {
  #define B(id_, name_, icon_, stat_, dev_) do { \
    if (_hass_walk_done) return; \
    HassBinaryRow r = {PSTR(id_), PSTR(name_), PSTR("{{ value_json." id_ " }}"), \
                        PSTR(icon_), stat_, dev_}; \
    (this->*fn)(r); \
  } while (0)

  B("warn",  "Warning", "mdi:alert-outline",          H_ST_SENSOR, H_DC_PROB);
  B("alert", "Alert",   "mdi:alert-octagram-outline", H_ST_SENSOR, H_DC_SAFETY);
#if GEIGER_IS_SERIAL(GEIGER_TYPE) && !GEIGER_IS_TEST(GEIGER_TYPE)
  B("ser_ok", "Serial Connected", "mdi:serial-port", H_ST_STATUS, H_DC_CONN);
#else
  B("tube", "Tube Active", "mdi:radioactive", H_ST_SENSOR, H_DC_CONN);
#endif

  #undef B
}

void MQTT_Client::hassPublishSensorAt(const HassSensorRow& r) {
  if (_hass_walk_done) return;
  if (_hass_walk_cursor == _hass_idx) {
    hassPublishSensor(r);
    _hass_walk_done = true;
  }
  _hass_walk_cursor++;
}

void MQTT_Client::hassPublishBinaryAt(const HassBinaryRow& r) {
  if (_hass_walk_done) return;
  if (_hass_walk_cursor == _hass_idx) {
    hassPublishBinarySensor(r);
    _hass_walk_done = true;
  }
  _hass_walk_cursor++;
}

void MQTT_Client::hassRemoveSensorAt(const HassSensorRow& r) {
  if (_hass_walk_done) return;
  if (_hass_walk_cursor == _hass_idx) {
    hassRemoveSensor(r);
    _hass_walk_done = true;
  }
  _hass_walk_cursor++;
}

void MQTT_Client::hassRemoveBinaryAt(const HassBinaryRow& r) {
  if (_hass_walk_done) return;
  if (_hass_walk_cursor == _hass_idx) {
    hassRemoveBinarySensor(r);
    _hass_walk_done = true;
  }
  _hass_walk_cursor++;
}

bool MQTT_Client::publishHassNext(uint8_t idx) {
  (void)idx;
  _hass_walk_cursor = 0;
  _hass_walk_done   = false;
  forEachHassSensor(&MQTT_Client::hassPublishSensorAt);
  if (!_hass_walk_done) forEachHassBinarySensor(&MQTT_Client::hassPublishBinaryAt);
  return _hass_walk_done;
}

bool MQTT_Client::removeHassNext(uint8_t idx) {
  (void)idx;
  _hass_walk_cursor = 0;
  _hass_walk_done   = false;
  forEachHassSensor(&MQTT_Client::hassRemoveSensorAt);
  if (!_hass_walk_done) forEachHassBinarySensor(&MQTT_Client::hassRemoveBinaryAt);
  return _hass_walk_done;
}

void MQTT_Client::setupHassCB() {
  _hass_listen = false;
  _pending &= ~PEND_SUB;
  if (!EGPrefs::getBool("mqtt", K_HASS_ENABLED)) return;
  const char* _discovery_topic = EGPrefs::getString("mqtt", K_HASS_TOPIC);
  if (_discovery_topic[0] == '\0') return;
  // onMqttMessage compares this
  snprintf_P(_hass_status, sizeof(_hass_status), H_STATUS_FMT, _discovery_topic);
  _hass_listen = true;
  _pending |= PEND_SUB;
}

void MQTT_Client::setupHassAuto() {
  if (_hass_walk_state) return;
  if (!EGPrefs::getBool("mqtt", K_HASS_ENABLED)) return;
  const char* _discovery_topic = EGPrefs::getString("mqtt", K_HASS_TOPIC);
  if (_discovery_topic[0] == '\0') return;
  _hass_walk_state = 1;
  _hass_idx = 0;
  Log::console(PSTR("MQTT: Starting HA autodiscovery walk"));
}

void MQTT_Client::setupHassRemove() {
  if (_hass_walk_state) return;
  const char* _discovery_topic = EGPrefs::getString("mqtt", K_HASS_TOPIC);
  if (_discovery_topic[0] == '\0') return;
  _hass_walk_state = -1;
  _hass_idx = 0;
  Log::console(PSTR("MQTT: Starting HA autodiscovery removal walk"));
}

void MQTT_Client::triggerHassDiscovery() {
  if (_hass_walk_state) return;
  unsigned long now_s = (unsigned long)DeviceInfo::uptime();
  if (_hass_last_publish && (now_s - _hass_last_publish) < MQTT_HASS_MIN_INTERVAL_S) {
    Log::debug(PSTR("MQTT: HA discovery throttled (%lu s since last)"),
               now_s - _hass_last_publish);
    return;
  }
  setupHassAuto();
  _hass_next_publish = now_s + MQTT_HASS_REFRESH_S;
  if (_hass_next_publish == 0) _hass_next_publish = 1;
}

void MQTT_Client::stepHassDiscovery() {
  if (_hass_walk_state == 0) return;
  // wait for acks, a refused one would be a sensor missing in HA
  if (s_mq.room() < HASS_PACKET_MAX) return;
  bool emitted = (_hass_walk_state > 0)
                  ? publishHassNext(_hass_idx)
                  : removeHassNext(_hass_idx);
  if (emitted) {
    _hass_idx++;
  } else {
    if (_hass_walk_state > 0) {
      _hass_last_publish = (unsigned long)DeviceInfo::uptime();
      if (_hass_last_publish == 0) _hass_last_publish = 1;
    }
    _hass_walk_state = 0;
  }
}

void MQTT_Client::hassPublishSensor(const HassSensorRow& r) {
  HassExtra extras[5] = {
    {H_K_STAT, r.stat_t},
    {H_K_VT,   r.val_tpl},
    {H_K_IC,   r.icon},
  };
  size_t n = 3;
  if (pgm_read_byte(r.unit)) extras[n++] = {H_K_UOM, r.unit};
  if (pgm_read_byte(r.precision)) extras[n++] = {H_K_PREC, r.precision};
  const HassTopic t = {H_TYPE_SENSOR, r.id, r.name,
    r.dev_cla, r.state_cla, r.ent_cat, H_EMPTY};
  publishHassTopic(t, extras, n);
}

void MQTT_Client::hassPublishBinarySensor(const HassBinaryRow& r) {
  HassExtra extras[] = {
    {H_K_STAT,  r.stat_t},
    {H_K_VT,    r.val_tpl},
    {H_K_PLON,  H_V_ONE},
    {H_K_PLOFF, H_V_ZERO},
    {H_K_IC,    r.icon},
  };
  const HassTopic t = {H_TYPE_BINSENS, r.id, r.name,
    r.dev_cla, H_EMPTY, H_EMPTY, H_EMPTY};
  publishHassTopic(t, extras, 5);
}

void MQTT_Client::hassRemoveSensor(const HassSensorRow& r) {
  removeHassTopic(H_TYPE_SENSOR, r.id);
}

void MQTT_Client::hassRemoveBinarySensor(const HassBinaryRow& r) {
  removeHassTopic(H_TYPE_BINSENS, r.id);
}

// all PGM_P
void MQTT_Client::publishHassTopic(const HassTopic& t,
    const HassExtra* extras, size_t n_extras)
{
  const char* host = DeviceInfo::hostname();
  char path[HASS_PATH_LEN];
  buildHassPath(path, sizeof(path), hass_disc(), t.type, t.id);
  size_t cap;
  char* out = json_begin(path, MQTT_JSON_BUFFER_SIZE, &cap);
  if (!out) {
    Log::console(PSTR("MQTT: HA discovery publish dropped: %s"), t.id);
    return;
  }

  char ipStr[EG_IPV4_MAX + 1];
  Wifi::formatIP(ipStr, sizeof(ipStr));
  EGJson j(out, cap);
  j.map_begin();
  j.map_begin(PSTR("device"));
  j.str(PSTR("mf"), PSTR(THING_NAME));
  j.str(PSTR("mdl"), DeviceInfo::chipmodel());
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
  j.str(PSTR("avty_t"), PSTR("~/tele/lwt"));
  j.str(PSTR("pl_avail"), PSTR("Online"));
  j.str(PSTR("pl_not_avail"), PSTR("Offline"));
  j.str(PSTR("~"), getRootTopic());
  j.str_begin(PSTR("name"));
  j.part(host);
  j.part(PSTR(" "));
  j.part(t.displayName);
  j.str_end();
  j.str_begin(PSTR("uniq_id"));
  j.part(host);
  j.part(PSTR("_"));
  j.part(t.id);
  j.str_end();
  if (pgm_read_byte(t.devCla))   j.str(PSTR("dev_cla"), t.devCla);
  if (pgm_read_byte(t.stateCla)) j.str(PSTR("stat_cla"), t.stateCla);
  if (pgm_read_byte(t.entCat))   j.str(PSTR("ent_cat"), t.entCat);
  if (pgm_read_byte(t.cmdTopic)) {
    j.str_begin(PSTR("cmd_t"));
    j.part(PSTR("~"));
    j.part(t.cmdTopic);
    j.str_end();
  }
  for (size_t i = 0; i < n_extras; i++) j.str(extras[i].key, extras[i].value);
  j.map_end();
  // partial is worse than none; not ended, nothing goes
  if (j.overflow) {
    Log::console(PSTR("MQTT: HA discovery too big, not sent: %s"), t.id);
    return;
  }
  if (s_mq.publish_end(j.length()) == 0) {
    Log::console(PSTR("MQTT: HA discovery publish dropped: %s"), t.id);
  }
  MQTT_PUB_YIELD();
}

void MQTT_Client::removeHassTopic(const char* type, const char* id)
{
  char path[HASS_PATH_LEN];
  buildHassPath(path, sizeof(path), hass_disc(), type, id);
  s_mq.publish(path, "", 1, false);
}
#endif

void MQTT_Client::onMqttMessage(const char* topic, const char* payload, size_t len)
{
#ifdef MQTTAUTODISCOVER
  if (_hass_listen && strcmp(_hass_status, topic) == 0) {
    if (len == 6 && strncmp_P(payload, PSTR("online"), 6) == 0) {
      Log::debug(PSTR("MQTT: HA is back online"));
      _hass_retrigger = true;
    }
  }
#else
  (void)topic; (void)payload; (void)len;
#endif
}

void MQTT_Client::begin()
{
  const char* _mqtt_server = EGPrefs::getString("mqtt", EG_K_SERVER);
  if (_mqtt_server[0] == '\0') {
    mqttEnabled = false;
    s_mq.stop();
    EGModuleRegistry::set_tick_enabled(this, false);
    EGModuleRegistry::set_loop_interval(this, -1);
    return;
  }
  mqttEnabled = true;
  s_mq.setKeepAlive(MQTT_KEEPALIVE_S);
  s_mq.setTimeout(MQTT_TIMEOUT_S);
  s_mq.onConnect(mq_up, this);
  s_mq.onDisconnect(mq_down, this);
  s_mq.onMessage(mq_message, this);
  s_mq.onAck(mq_ack, this);
  EGModuleRegistry::set_tick_enabled(this, true);
  EGModuleRegistry::set_loop_interval(this, 500);
}

void MQTT_Client::on_prefs_saved()
{
  _rootTopicCached = false;
  bool needs_hass_remove = false;
#ifdef MQTTAUTODISCOVER
  needs_hass_remove = (connected && !EGPrefs::getBool("mqtt", K_HASS_ENABLED));
#endif
  disconnect();
  reconnectAttempts = 0;
  authFailures = 0;
  lastConnectionAttempt = 0;
#ifdef MQTTAUTODISCOVER
  // publish walk restarts on reconnect
  if (_hass_walk_state > 0) _hass_walk_state = 0;
  if (needs_hass_remove) setupHassRemove();
#endif
  begin();
}
#endif
