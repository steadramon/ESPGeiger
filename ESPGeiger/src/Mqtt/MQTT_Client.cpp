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
#include "Corium/System/Yield.h"
#include "Corium/Util/Words.h"
#include "Corium/Platform/Chip.h"
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
static bool s_drop_told = false;

// Logged once a boot
static void note_dropped() {
  if (s_drop_told || !s_mq.dropped()) return;
  s_drop_told = true;
  corium::Log::console(PSTR("MQTT: Message over %u B dropped"), (unsigned)EGMqtt::IN_CAP);
}

MQTT_Client& mqtt = MQTT_Client::getInstance();
CORIUM_REGISTER_FEATURE(mqtt, FEAT_MQTT)

#define MQTT_PUB_YIELD() corium::yieldAfter(10000)

// Max size of each payload, anything longer is cut
static const size_t STATUS_MAX = 512;
static const size_t SENSOR_MAX = 384;

// Writes straight into the outbox, nullptr if there's no room for max
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
CORIUM_PSTR(MQ_L_TPC, "Root topic");
CORIUM_PSTR(MQ_H_TPC, "{id} = chip ID");
CORIUM_PSTR(MQ_P_TPC, "/?[A-Za-z0-9_\\/\\{\\}\\-]+");   // a bare / refused
#ifdef MQTTAUTODISCOVER
CORIUM_PSTR(MQ_L_HEN, "HA autodiscovery");
CORIUM_PSTR(MQ_L_HTP, "HA autodiscovery topic");
CORIUM_PSTR(MQ_H_HTP, "HA discovery prefix");
CORIUM_PSTR(MQ_P_HTP, "/?[A-Za-z0-9_\\/\\-]+");
static void hass_prefix(char* out, size_t sz);
#endif

static const corium::Pref MQTT_PREF_ITEMS[] PROGMEM = {
  {corium::K_SERVER,    MQ_L_SRV, MQ_H_SRV, "",               corium::P_HOST, 0, 0, CORIUM_HOST_MAX, corium::P_STRING, 0},
  {EG_K_PORT,      EG_L_PRT, nullptr,  "1883",           nullptr,  1, 65535, 0,  corium::P_UINT,   0},
  {EG_K_USER,      MQ_L_USR, nullptr,  "",               nullptr,  0, 0,     MQTT_USER_MAX, corium::P_STRING, 0},
  {EG_K_PASSWORD,  EG_L_PWD, nullptr,  "",               nullptr,  0, 0,     MQTT_PASS_MAX, corium::P_STRING, corium::P_SENSITIVE},
  {K_TOPIC,        MQ_L_TPC, MQ_H_TPC, "ESPGeiger-{id}", MQ_P_TPC, 0, 0, MQTT_TOPIC_PREF_MAX, corium::P_STRING, corium::P_ADVANCED},
  {EG_K_INTERVAL,  EG_L_INT, EG_H_INT, STR(MQTT_INTERVAL), nullptr, MQTT_MIN_TIME, MQTT_MAX_TIME, 0, corium::P_UINT, 0},
#ifdef MQTTAUTODISCOVER
  {K_HASS_ENABLED, MQ_L_HEN, nullptr,  MQTT_HASS_DEFAULT,    nullptr,  0, 0, 0,  corium::P_BOOL,   corium::P_ADVANCED},
  {K_HASS_TOPIC,   MQ_L_HTP, MQ_H_HTP, MQTT_DISCOVERY_TOPIC, MQ_P_HTP, 0, 0, MQTT_HASS_TOPIC_MAX, corium::P_STRING, corium::P_ADVANCED},
#endif
};

static const corium::PrefGroup MQTT_PREF_GROUP PROGMEM = {
  "mqtt", "MQTT", 1,
  MQTT_PREF_ITEMS,
  sizeof(MQTT_PREF_ITEMS) / sizeof(MQTT_PREF_ITEMS[0]),
  corium::P_CAT_OUTPUT, corium::K_SERVER, 0,
};

const corium::PrefGroup* MQTT_Client::prefsGroup() { return &MQTT_PREF_GROUP; }

// On when a server is set, same as the /param badge
static bool mqtt_on() { return corium::Prefs::groupEnabled(&MQTT_PREF_GROUP); }

size_t MQTT_Client::statusJson(char* buf, size_t cap) {
  if (!mqtt_on()) return 0;
  return writeStatusJson(buf, cap, PSTR("mqtt"), lastOk && s_mq.connected(), lastAttempt);
}

// legacy import
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
corium::LegacyRows MQTT_Client::legacyAliases() { return MQTT_LEGACY; }

MQTT_Client::MQTT_Client()
{
}

// The only way out. Sends Offline first, a refused rejoin would never say it
void MQTT_Client::disconnect()
{
  bool drop = moved();
  s_mq.disconnect(true);
#ifdef MQTTAUTODISCOVER
  char disc[sizeof(_hass_disc)];
  hass_prefix(disc, sizeof(disc));
  if (strcmp(disc, _hass_disc) != 0) drop = true;   // HA prefix changed, its old configs go too
  _hass_next.clear();
  _hass_walk_state = HASS_IDLE;   // a publish walk starts again on connect
#endif
  if (drop) s_mq.stop();   // the kept QoS 1 queue was for the old broker
  _enabled = true;
  _tries = 0;
  _tried_ms = 0;
  _root_ok = false;
}

void MQTT_Client::onMqttConnect(bool sessionPresent) {
  corium::Log::console(PSTR("MQTT: Connected"));
  _reanchor = true;
  _tries = 0;
  _auth_fails = 0;
  // Send warn and alert again, a clear may have been lost with a restart
  _pending |= PEND_LWT | PEND_WARN | PEND_ALERT;
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
  bool refused = err == EGMqtt::BAD_CREDENTIALS || err == EGMqtt::NOT_AUTHORIZED;
  corium::Log::console(PSTR("MQTT: Disconnected (%s)"), disconnect_reason_text(err));
  unsigned long now_ms = corium::fastMillis();
  bool latched = false;
  if (refused && ++_auth_fails >= MQTT_AUTH_TRIES) {
    _enabled = false;
    s_mq.stop();   // free the memory until the settings change
    latched = true;
  }
  _tried_ms = now_ms;
  wakeSoon();
  if (latched) corium::Log::console(PSTR("MQTT: Credentials rejected " STR(MQTT_AUTH_TRIES) " times, stopped until settings are saved"));
#ifdef MQTTAUTODISCOVER
  if (_hass_walk_state == HASS_REMOVE) rejoin();   // dropped while leaving, carry on to the new settings
  else if (_hass_walk_state != HASS_IDLE) _hass_idx = 0;
#endif
}

unsigned long MQTT_Client::statusSlotAnchor(unsigned long now_s) {
  if (_slot_s < 0)
    _slot_s = (int16_t)(corium::SendSlot::initialOffset(name(), _status_every * 1000UL) / 1000UL);
  uint32_t off_s = (uint32_t)_slot_s;
  uint32_t until = off_s;
  if (corium::Clock::synced) until = corium::SendSlot::untilPhase(off_s, corium::Clock::phase(_status_every), _status_every);
  return now_s + until - _status_every;
}

#ifdef MQTTAUTODISCOVER
static uint32_t hass_spread_offset_s() {
  return corium::DeviceInfo::chipHash() % (MQTT_HASS_BIRTH_SPREAD_S + 1);
}

void MQTT_Client::scheduleHassDiscovery(bool reconnected)
{
  const unsigned long up_s = (unsigned long)corium::DeviceInfo::uptime();
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

// Fires once now - last > interval and stays on the slot, or restarts from now if a whole interval behind
static bool fired(unsigned long now, unsigned long& last, uint16_t interval)
{
  if ((now - last) <= interval) return false;
  last += interval;
  if ((now - last) > interval) last = now;
  return true;
}

void MQTT_Client::queueTimedPublishes(unsigned long now_s)
{
  if (fired(now_s, _status_s, _status_every)) _pending |= PEND_STATUS;
  if (fired(now_s, _ping_s, _ping_every))     _pending |= PEND_PING;
}

void MQTT_Client::second(unsigned long now_s)
{
  if (!_enabled) {
    return;
  }

  if (!s_mq.connected()) {
    _pending |= PEND_CONNECT;
    return;
  }

  const bool reconnected = _reanchor;
  if (reconnected) {
    _reanchor = false;
    // Ping half a slot after status
    _status_s = statusSlotAnchor(now_s);
    _ping_s   = _status_s + _status_every + (_status_every / 2) - _ping_every;
  }

#ifdef MQTTAUTODISCOVER
  scheduleHassDiscovery(reconnected);
#endif

  queueTimedPublishes(now_s);
}

// Only once warm, with a full CPM bucket
void MQTT_Client::checkAlarms()
{
  if (!(_pending & PEND_ALARMS) || !gcounter.is_warm()) return;
  _pending &= ~PEND_ALARMS;
  bool warn = gcounter.is_warning();
  if (warn != _warn_sent) {
    _warn_sent = warn;
    _pending |= PEND_WARN;
  }
  bool alert = gcounter.is_alert();
  if (alert != _alert_sent) {
    _alert_sent = alert;
    _pending |= PEND_ALERT;
  }
}

void MQTT_Client::loop(unsigned long now)
{
  timers(now);   // before the client's callbacks
  s_mq.loop();   // its callbacks run from here
  note_dropped();
  if (corium::Registry::events(this)) _pending |= PEND_ALARMS;
  if (!_enabled) return;
  if (_pending & PEND_CONNECT) { _pending &= ~PEND_CONNECT; reconnect(); return; }
  if (!s_mq.connected() || leaving()) return;
  checkAlarms();
  // One publish per call, these stay pending until the outbox takes them
  if (sendHello()) return;
  if (_pending & PEND_WARN)       { if (publishFlag(PSTR("WARN"), _warn_sent))   _pending &= ~PEND_WARN;  return; }
  if (_pending & PEND_ALERT)      { if (publishFlag(PSTR("ALERT"), _alert_sent)) _pending &= ~PEND_ALERT; return; }
  // if there's no room for status, ping is smaller and can still go
  if ((_pending & PEND_STATUS) && publishStatus()) { _pending &= ~PEND_STATUS; return; }
  if (_pending & PEND_PING)       { _pending &= ~PEND_PING;   publishPing();   return; }
#ifdef MQTTAUTODISCOVER
  stepHassDiscovery();
#endif
}

// While leaving only the removals go, to the old broker
bool MQTT_Client::leaving()
{
#ifdef MQTTAUTODISCOVER
  if (_hass_walk_state == HASS_REMOVE) {
    stepHassDiscovery();
    return true;
  }
#endif
  return false;
}

// Online, then the HA subscribe
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
    char t[sizeof(_hass_disc) + sizeof("/status")];
    snprintf_P(t, sizeof(t), PSTR("%s/status"), _hass_disc);
    if (s_mq.subscribe(t, 1)) _pending &= ~PEND_SUB;
    return true;
  }
#endif
  return false;
}

// stat/WARN, stat/ALERT
bool MQTT_Client::publishFlag(const char* cmd, bool on)
{
  return publishStat(cmd, on ? "1" : "0");
}

bool MQTT_Client::publishStatus()
{
  char topic[MQTT_TOPIC_LEN];
  buildTopic(topic, sizeof(topic), T_TELE, PSTR("status"));
  size_t cap;
  char* out = json_begin(topic, STATUS_MAX, &cap);
  if (!out) {
    // count it as failed once, then wait for room
    if (!_status_late) track(0);
    _status_late = true;
    return false;
  }
  _status_late = false;
  corium::Registry::emit(TOPIC_SENT);
  // Left blank before NTP, the time would give a bad year
  char dateTime[sizeof("2026-01-01T00:00:00Z")];
  dateTime[0] = '\0';
  struct tm t;
  if (corium::Clock::utcTm(&t)) {
    snprintf_P(dateTime, sizeof(dateTime), PSTR("%04d-%02d-%02dT%02d:%02d:%02dZ"),
      1900 + t.tm_year, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  }

  char ip[CORIUM_IPV4_MAX + 1];
  corium::Wifi::formatIP(ip, sizeof(ip));
  EGJson j(out, cap);
  j.map_begin();
  j.str(PSTR("time"), dateTime);
  j.uint(PSTR("ut"), (uint32_t)corium::DeviceInfo::uptime());
  j.str(PSTR("board"), corium::DeviceInfo::board());
  j.str(PSTR("model"), Tube::model());
  j.str(PSTR("ssid"), corium::Wifi::ssid);
  j.str(PSTR("ip"), ip);
  j.sint(PSTR("rssi"), (int32_t)corium::Wifi::rssi);
  j.uint(PSTR("c_total"), (uint32_t)gcounter.total_clicks);
  j.uint(PSTR("tick"), corium::TickProfile::tickUs);
  j.uint(PSTR("t_max"), corium::TickProfile::tickMaxUs);
  j.uint(PSTR("lps"), corium::TickProfile::lps);
#ifdef MQTT_MEM_DEBUG
  #ifdef ESP8266
    uint32_t heap_free;
    uint16_t heap_max;
    uint8_t heap_frag;
    corium::DeviceInfo::heapStats(&heap_free, &heap_max, &heap_frag);
    j.uint(PSTR("free_mem"), heap_free);
    j.uint(PSTR("heap_max"), heap_max);
    j.uint(PSTR("heap_frag"), heap_frag);
  #else
    j.uint(PSTR("free_mem"), corium::DeviceInfo::freeHeap());
    j.uint(PSTR("min_free"), corium::DeviceInfo::heapMin());
    j.uint(PSTR("heap_max"), corium::DeviceInfo::heapMaxBlock());
  #endif
#else
  j.uint(PSTR("free_mem"), (uint32_t)corium::DeviceInfo::freeHeap());
#endif
#if CORIUM_HAS_CHIPTEMP
  j.fixed(PSTR("cpu_temp"), corium::DeviceInfo::chipTempC(), 1);
#endif
#if GEIGER_IS_SERIAL(GEIGER_TYPE) && !GEIGER_IS_TEST(GEIGER_TYPE)
  j.uint(PSTR("ser_ok"), gcounter.is_healthy() ? 1 : 0);
#endif
  j.map_end();
  if (j.overflow) corium::Log::console(PSTR("MQTT: Status cut to fit"));
  track(s_mq.publish_end(j.length()));
  corium::Log::debug(PSTR("MQTT: Published"));
  return true;
}

// Counts as ok once the broker acks it, not when it's queued
void MQTT_Client::track(uint16_t pid)
{
  if (_ack_pid) noteResult(false);   // the last one never came back
  _ack_pid = pid;
  if (pid) noteAttempt();
  else notePublish(false);
}

void MQTT_Client::onMqttAck(uint16_t id)
{
  if (!_ack_pid || id != _ack_pid) return;
  _ack_pid = 0;
  noteResult(true);
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

// legacy stat/ topics
struct StatTopic { char id[6]; char topic[6]; };
static const StatTopic STAT_TOPICS[] PROGMEM = {
  {"cpm", "CPM"}, {"cpm5", "CPM5"}, {"cpm15", "CPM15"}, {"usv", "uSv"},
#ifndef DISABLE_MQTT_CPS
  {"cps", "CPS"},
#endif
#if M_HAS_HV
  {"hv", "HV"},
#endif
  {"t", "T"}, {"h", "H"}, {"p", "P"},
};

static const char* stat_topic(const char* id) {
  for (const StatTopic& s : STAT_TOPICS) {
    if (strcmp_P(id, s.id) == 0) return s.topic;
  }
  return nullptr;
}

bool MQTT_Client::publishStat(const char* cmd, const char* text)
{
  char topic[MQTT_TOPIC_LEN];
  buildTopic(topic, sizeof(topic), T_STAT, cmd);
  return s_mq.publish(topic, text, 1, false);
}

void MQTT_Client::publishPing()
{
  if (!publishSensor()) notePublish(false);

  for (corium::MeasureIter it(EGM_OUT_MQTT); it.next(); ) {
    const char* topic = stat_topic(it.row().id);
    char text[24];
    if (!topic || corium::m_format(it.row(), it.value(), text, sizeof(text)) == 0) continue;
    publishStat(topic, text);
    MQTT_PUB_YIELD();
  }

  corium::Registry::emit(TOPIC_PING);
}

// a stalled connect times out in EGMqtt
bool MQTT_Client::clientBusy()
{
  return s_mq.connected() || s_mq.state() == EGMqtt::CONNECTING;
}

static unsigned long reconnect_backoff_ms(uint8_t attempts)
{
  unsigned long backoff = MQTT_RETRY_MS << min(attempts, (uint8_t)5);
  if (backoff > MQTT_RETRY_MAX_MS) backoff = MQTT_RETRY_MAX_MS;
  backoff += (corium::DeviceInfo::chipHash() % (MQTT_JITTER_S + 1u)) * 1000u;
  return backoff;
}

// These are all called from s_mq.loop()
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
  int secs = (int)corium::Prefs::getUInt("mqtt", EG_K_INTERVAL);
  if (secs != _ping_every) {
    _ping_every = (uint16_t)secs;
    corium::Log::console(PSTR("MQTT: Submission Interval %d seconds"), (int)_ping_every);
  }
}

static bool in_backoff(unsigned long last, unsigned long now, uint8_t attempts)
{
  unsigned long backoff = reconnect_backoff_ms(attempts);
  return last && now - last < backoff;
}

// Same rule as queueTimedPublishes, fires once now - last > interval
static uint32_t left_s(unsigned long last, uint16_t interval, unsigned long now)
{
  if ((now - last) > interval) return 1;
  return (uint32_t)(last + interval + 1 - now);
}

// Only runs when a timer is due
void MQTT_Client::timers(unsigned long now)
{
  if (!corium::reachedMax(now, _due_ms, DUE_MAX_MS)) return;
  // use uptime, ms / 1000 wraps at 49.7 days
  unsigned long up_s = (unsigned long)corium::DeviceInfo::uptime();
  second(up_s);
  _due_ms = (now / 1000 + nextDue(now, up_s)) * 1000;
}

// Seconds to the next due timer, at least 1
uint32_t MQTT_Client::nextDue(unsigned long now_ms, unsigned long now_s)
{
  if (!_enabled) return 3600;   // begin() wakes it
  if (!s_mq.connected()) {
    // each second while connecting
    if (!_tried_ms || s_mq.state() == EGMqtt::CONNECTING) return 1;
    uint32_t at = _tried_ms + reconnect_backoff_ms(_tries);
    long s = (long)((at + 999) / 1000 - now_ms / 1000);
    return s > 0 ? (uint32_t)s : 1;
  }
  if (_reanchor) return 1;
  uint32_t s = left_s(_status_s, _status_every, now_s);
  uint32_t p = left_s(_ping_s, _ping_every, now_s);
  if (p < s) s = p;
#ifdef MQTTAUTODISCOVER
  if (_hass_retrigger) return 1;
  if (_hass_next.armed()) {
    long h = (long)(_hass_next.at - now_s);
    if (h < (long)s) s = h > 0 ? (uint32_t)h : 1;
  }
#endif
  return s;
}

// A callback moved a timer, check again on the next second
void MQTT_Client::wakeSoon()
{
  _due_ms = (corium::fastMillis() / 1000 + 1) * 1000;
}

static int mqtt_port()
{
  int port = (int)corium::Prefs::getUInt("mqtt", EG_K_PORT);
  return port > 0 ? port : 1883;
}

// true if the prefs differ from what the connection was set up with
bool MQTT_Client::moved()
{
  if (s_mq.port() != mqtt_port() ||
      strcmp(corium::Prefs::getString("mqtt", corium::K_SERVER), s_mq.host()) != 0) return true;
  char lwt[MQTT_TOPIC_LEN];
  buildTopic(lwt, sizeof(lwt), T_TELE, PSTR("lwt"));
  return strcmp(lwt, s_mq.willTopic()) != 0;
}

// MQTT 3.1.1 has no password without a user
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

  unsigned long now = corium::fastMillis();
  if (in_backoff(_tried_ms, now, (uint8_t)_tries)) return;
  _tried_ms = now;

  if (!mqtt_on()) {
    _enabled = false;
    s_mq.stop();
    return;
  }
  const char* server = corium::Prefs::getString("mqtt", corium::K_SERVER);
  int port = mqtt_port();

  // Set every time so a topic change is picked up
  char lwt[MQTT_TOPIC_LEN];
  buildTopic(lwt, sizeof(lwt), T_TELE, PSTR("lwt"));
  bool set = s_mq.setServer(server, (uint16_t)port) &&
             s_mq.setClientId(corium::DeviceInfo::hostname()) &&
             s_mq.setWill(lwt, FPSTR(LWT_OFFLINE), 1, true) &&
             set_credentials();

  applyIntervalPref();
  if (_tries < 10) _tries++;

  if (!set || !s_mq.connect()) {
    corium::Log::console(PSTR("MQTT: Connect refused by client (attempt %d)"), _tries);
    return;
  }
  corium::Log::console(PSTR("MQTT: Connecting (attempt %d) ... %s:%d"), _tries, server, port);
}

// For both topic prefs: {id} in any case is replaced, no trailing /
static void topic_pref(char* out, size_t sz, const char* pref)
{
  const char* chipId = corium::DeviceInfo::chipId();
  size_t w = 0;
  const size_t cap = sz - 1;
  for (const char* p = pref; *p && w < cap; ) {
    if (strncasecmp_P(p, PSTR("{id}"), 4) == 0) {
      size_t n = strlen(chipId);
      if (w + n > cap) n = cap - w;
      memcpy(out + w, chipId, n);
      w += n;
      p += 4;
    } else {
      out[w++] = *p++;
    }
  }
  while (w > 0 && out[w-1] == '/') w--;
  out[w] = '\0';
}

const char* MQTT_Client::getRootTopic()
{
  if (!_root_ok) {
    topic_pref(_root, sizeof(_root), corium::Prefs::getString("mqtt", K_TOPIC));
    _root_ok = true;
  }
  return _root;
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
// entity_category
static const char H_EC_DIAG[]   PROGMEM = "diagnostic";

// device_class
#define DC_NONE   ""
#define DC_SIG    "signal_strength"
#define DC_DUR    "duration"
#define DC_PROB   "problem"
#define DC_SAFETY "safety"
#define DC_CONN   "connectivity"
#define DC_TEMP   "temperature"
#define DC_HUMID  "humidity"
#define DC_PRES   "atmospheric_pressure"

// Row flags, one letter each
#define HR_STATUS "s"   // tele/status, else tele/sensor
#define HR_TOTAL  "t"   // total_increasing, else measurement
#define HR_DIAG   "d"
#define HR_BIN    "b"   // binary_sensor
#define HR_OPT    "o"   // only with its measure row
#define HR_TEXT   "w"   // text value, no state_class

// key "" = id, icon after mdi:, prec "" = HA default
#define HROW(id, key, name, unit, icon, prec, dev, flags) \
  id "\0" key "\0" name "\0" unit "\0" icon "\0" prec "\0" dev "\0" flags "\0"
enum { F_ID, F_KEY, F_NAME, F_UNIT, F_ICON, F_PREC, F_DEV, F_FLAGS, F_ROW };

// In walk order, an empty id ends it
static const char HASS_ROWS[] PROGMEM =
  // tele/sensor
  HROW("cpm",      "",   "CPM",          "CPM",        "pulse",          "",  DC_NONE,  "")
  HROW("cpm5",     "",   "CPM5",         "CPM",        "pulse",          "",  DC_NONE,  "")
  HROW("cpm15",    "",   "CPM15",        "CPM",        "pulse",          "",  DC_NONE,  "")
  HROW("usv",      "",   "\u00B5Sv/h",   "\u00B5Sv/h", "radioactive",    "2", DC_NONE,  "")
#if M_HAS_HV
  HROW("hv",       "",   "HV",           "V",          "lightning-bolt", "",  DC_NONE,  HR_OPT)
#endif
  HROW("temp",     "t",  "Temperature",  "°C",         "thermometer",    "",  DC_TEMP,  HR_OPT)
  HROW("humidity", "h",  "Humidity",     "%",          "water-percent",  "",  DC_HUMID, HR_OPT)
  HROW("pressure", "p",  "Pressure",     "hPa",        "gauge",          "",  DC_PRES,  HR_OPT)
  HROW("c_total",  "",   "Total Clicks", "",           "counter",        "",  DC_NONE,  HR_STATUS HR_TOTAL)
  // tele/status
  HROW("tick",     "",   "Tick",         "\u00B5s",    "timer-outline",  "",  DC_NONE,  HR_STATUS HR_DIAG)
  HROW("t_max",    "",   "Max Tick",     "\u00B5s",    "timer-alert-outline", "", DC_NONE, HR_STATUS HR_DIAG)
  HROW("lps",      "",   "LPS",          "1/s",        "speedometer",    "",  DC_NONE,  HR_STATUS HR_DIAG)
  HROW("rssi",     "",   "RSSI",         "dBm",        "wifi",           "",  DC_SIG,   HR_STATUS HR_DIAG)
  HROW("uptime",   "ut", "Uptime",       "s",          "clock-outline",  "",  DC_DUR,   HR_STATUS HR_TOTAL HR_DIAG)
  HROW("free_mem", "",   "Free Memory",  "B",          "memory",         "",  DC_NONE,  HR_STATUS HR_DIAG)
#if CORIUM_HAS_CHIPTEMP
  HROW("cpu_temp", "",   "CPU Temperature", "°C",      "chip",           "1", DC_TEMP,  HR_STATUS HR_DIAG)
#endif
  // binary
  HROW("warn",     "",   "Warning",      "",           "alert-outline",  "",  DC_PROB,  HR_BIN)
  HROW("alert",    "",   "Alert",        "",           "alert-octagram-outline", "", DC_SAFETY, HR_BIN)
#if GEIGER_IS_SERIAL(GEIGER_TYPE) && !GEIGER_IS_TEST(GEIGER_TYPE)
  HROW("ser_ok",   "",   "Serial Connected", "",       "serial-port",    "",  DC_CONN,  HR_BIN HR_STATUS)
#else
  HROW("tube",     "",   "Tube Active",  "",           "radioactive",    "",  DC_CONN,  HR_BIN)
#endif
  HROW("problem",  "",   "Tube Problem", "",           "alert-circle-outline", "", DC_PROB, HR_BIN)
  HROW("health",   "",   "Health",       "",           "heart-pulse",    "",  DC_NONE,  HR_DIAG HR_TEXT)
#if M_HAS_HV
  HROW("hv_fault", "",   "HV Fault",     "",           "flash-alert",    "",  DC_PROB,  HR_BIN HR_OPT)
#endif
  ;

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

static const size_t HASS_PATH_LEN = 128;
// Payload, path, MQTT header and packet id
static const size_t HASS_PACKET_MAX = MQTT_JSON_BUFFER_SIZE + HASS_PATH_LEN + 16;

// HA prefix from the prefs, "" if off or blank
static void hass_prefix(char* out, size_t sz) {
  out[0] = '\0';
  if (corium::Prefs::getBool("mqtt", K_HASS_ENABLED)) topic_pref(out, sz, corium::Prefs::getString("mqtt", K_HASS_TOPIC));
}

// id PROGMEM, absent or NaN = no entity
static bool has_row(const char* id) {
  corium::MeasureIter it(EGM_OUT_MQTT, false, id);
  return it.next();
}

static int hass_path(char* buf, size_t sz, const char* disc,
                          const char* type, const char* name) {
  return snprintf_P(buf, sz, PSTR("%s/%s/%s-%s/config"), disc, type, corium::DeviceInfo::hostname(), name);
}

static const char* hass_field(const char* r, uint8_t n) {
  while (n--) r += strlen_P(r) + 1;
  return r;
}

static bool hass_is(const char* r, char flag) {
  for (const char* f = hass_field(r, F_FLAGS); pgm_read_byte(f); f++) {
    if (pgm_read_byte(f) == flag) return true;
  }
  return false;
}

static const char* hass_key(const char* r) {
  const char* k = hass_field(r, F_KEY);
  return pgm_read_byte(k) ? k : r;
}

// nullptr past the end
static const char* hass_row(uint8_t idx) {
  for (const char* r = HASS_ROWS; pgm_read_byte(r); r = hass_field(r, F_ROW)) {
    if (hass_is(r, *HR_OPT) && !has_row(hass_key(r))) continue;
    if (idx-- == 0) return r;
  }
  return nullptr;
}

static const char* hass_type(const char* r) {
  return hass_is(r, *HR_BIN) ? H_TYPE_BINSENS : H_TYPE_SENSOR;
}

// Prefix is kept until the next connect, walks and saves go by it
void MQTT_Client::setupHassCB() {
  if (_hass_gone) {   // saved while down, remove the old prefix's entities first
    _hass_gone = false;
    startHassWalk(HASS_REMOVE);
    return;
  }
  hass_prefix(_hass_disc, sizeof(_hass_disc));
  if (_hass_disc[0]) _pending |= PEND_SUB;
  else _pending &= ~PEND_SUB;
}

// Nothing to walk with HA off
void MQTT_Client::startHassWalk(int8_t dir) {
  if (_hass_walk_state != HASS_IDLE || !_hass_disc[0]) return;
  _hass_walk_state = dir;
  _hass_idx = 0;
  corium::Log::console(PSTR("MQTT: Starting HA autodiscovery %swalk"), dir == HASS_REMOVE ? PSTR("removal ") : PSTR(""));
}

void MQTT_Client::triggerHassDiscovery() {
  if (_hass_walk_state != HASS_IDLE) return;
  unsigned long now_s = (unsigned long)corium::DeviceInfo::uptime();
  if (corium::fresh(now_s, _hass_last_s, MQTT_HASS_MIN_INTERVAL_S)) {
    corium::Log::debug(PSTR("MQTT: HA discovery throttled (%lu s since last)"),
                       now_s - _hass_last_s);
    return;
  }
  startHassWalk(HASS_PUBLISH);
  _hass_next.set(now_s + MQTT_HASS_REFRESH_S);
}

static void hass_device(EGJson& j, const char* host) {
  char ip[CORIUM_IPV4_MAX + 1];
  corium::Wifi::formatIP(ip, sizeof(ip));
  j.map_begin(PSTR("device"));
  j.str(PSTR("mf"), APP_NAME);
  j.str(PSTR("mdl"), corium::DeviceInfo::board());
  j.str(PSTR("mdl_id"), PSTR(BUILD_ENV));
  j.str(PSTR("name"), host);
  j.str(PSTR("sw"), PSTR(RELEASE_VERSION "/" GIT_VERSION));
  j.str(PSTR("sn"), corium::DeviceInfo::chipId());
  j.array_begin(PSTR("identifiers"));
  j.str(nullptr, host);
  j.array_end();
  j.array_begin(PSTR("connections"));
  j.array_begin();
  j.str(nullptr, PSTR("mac"));
  j.str(nullptr, corium::DeviceInfo::mac());
  j.array_end();
  j.array_end();
  j.str_begin(PSTR("cu"));
  j.part(PSTR("http://"));
  j.part(ip);
  j.part(PSTR("/"));
  j.str_end();
  j.map_end();
}

static void hass_fields(EGJson& j, const char* r) {
  bool bin = hass_is(r, *HR_BIN);
  const char* dev = hass_field(r, F_DEV);
  if (pgm_read_byte(dev)) j.str(PSTR("dev_cla"), dev);
  if (!bin && !hass_is(r, *HR_TEXT)) j.str(PSTR("stat_cla"), hass_is(r, *HR_TOTAL) ? H_SC_TOTINC : H_SC_MEAS);
  if (hass_is(r, *HR_DIAG)) j.str(PSTR("ent_cat"), H_EC_DIAG);
  j.str(H_K_STAT, hass_is(r, *HR_STATUS) ? H_ST_STATUS : H_ST_SENSOR);
  j.str_begin(H_K_VT);
  j.part(PSTR("{{ value_json."));
  j.part(hass_key(r));
  // shows as unknown in HA when the value isn't there
  if (hass_is(r, *HR_OPT)) j.part(PSTR(" | default(None)"));
  j.part(PSTR(" }}"));
  j.str_end();
  if (bin) {
    j.str(H_K_PLON, H_V_ONE);
    j.str(H_K_PLOFF, H_V_ZERO);
  }
  j.str_begin(H_K_IC);
  j.part(PSTR("mdi:"));
  j.part(hass_field(r, F_ICON));
  j.str_end();
  const char* unit = hass_field(r, F_UNIT);
  if (pgm_read_byte(unit)) j.str(H_K_UOM, unit);
  const char* prec = hass_field(r, F_PREC);
  if (pgm_read_byte(prec)) j.str(H_K_PREC, prec);
}

CORIUM_PSTR(HA_DROPPED, "MQTT: HA discovery publish dropped: %s");

static void hass_publish(const char* r, const char* root, const char* disc)
{
  const char* host = corium::DeviceInfo::hostname();
  char path[HASS_PATH_LEN];
  hass_path(path, sizeof(path), disc, hass_type(r), r);
  size_t cap;
  char* out = json_begin(path, MQTT_JSON_BUFFER_SIZE, &cap);
  if (!out) {
    corium::Log::console(HA_DROPPED, r);
    return;
  }

  EGJson j(out, cap);
  j.map_begin();
  hass_device(j, host);
  j.str(PSTR("avty_t"), PSTR("~/tele/lwt"));
  j.str(PSTR("pl_avail"), LWT_ONLINE);
  j.str(PSTR("pl_not_avail"), LWT_OFFLINE);
  j.str(PSTR("~"), root);
  j.str(PSTR("name"), hass_field(r, F_NAME));
  j.str_begin(PSTR("uniq_id"));
  j.part(host);
  j.part(PSTR("_"));
  j.part(r);
  j.str_end();
  hass_fields(j, r);
  j.map_end();
  // A partial config is worse than none. Not ended, so nothing is sent
  if (j.overflow) {
    corium::Log::console(PSTR("MQTT: HA discovery too big, not sent: %s"), r);
    return;
  }
  if (s_mq.publish_end(j.length()) == 0) {
    corium::Log::console(HA_DROPPED, r);
  }
  MQTT_PUB_YIELD();
}

static void hass_remove(const char* r, const char* disc)
{
  char path[HASS_PATH_LEN];
  hass_path(path, sizeof(path), disc, hass_type(r), r);
  s_mq.publish(path, "", 1, false);
}

void MQTT_Client::stepHassDiscovery() {
  if (_hass_walk_state == HASS_IDLE) return;
  // Wait for acks to free room, a refused one would be a sensor missing in HA
  if (s_mq.room() < HASS_PACKET_MAX) return;
  const char* r = hass_row(_hass_idx);
  if (r) {
    if (_hass_walk_state == HASS_PUBLISH) hass_publish(r, getRootTopic(), _hass_disc);
    else hass_remove(r, _hass_disc);
    _hass_idx++;
  } else if (_hass_walk_state == HASS_PUBLISH) {
    _hass_last_s = (unsigned long)corium::DeviceInfo::uptime();
    if (_hass_last_s == 0) _hass_last_s = 1;
    _hass_walk_state = HASS_IDLE;
  } else if (!s_mq.queued()) {
    rejoin();   // all acked and removed from the old prefix
  }
}
#endif

void MQTT_Client::onMqttMessage(const char* topic, const char* payload, size_t len)
{
#ifdef MQTTAUTODISCOVER
  size_t n = strlen(_hass_disc);
  if (n && strncmp(topic, _hass_disc, n) == 0 && strcmp_P(topic + n, PSTR("/status")) == 0) {
    if (len == 6 && strncmp_P(payload, PSTR("online"), 6) == 0) {
      corium::Log::debug(PSTR("MQTT: HA is back online"));
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
  if (!mqtt_on()) {
    _enabled = false;
    s_mq.stop();
    corium::Registry::every(this, 0);
    return;
  }
  _enabled = true;
  wakeSoon();
  s_mq.setKeepAlive(MQTT_KEEPALIVE_S);
  s_mq.setTimeoutS(MQTT_TIMEOUT_S);
  s_mq.onConnect(mq_up, this);
  s_mq.onDisconnect(mq_down, this);
  s_mq.onMessage(mq_message, this);
  s_mq.onAck(mq_ack, this);
  corium::Registry::every(this, LOOP_MS);
  corium::Registry::on(Counter::TOPIC_ALARM, this);
  _pending |= PEND_ALARMS;   // in case an alarm is already up when it's turned on
}

// Disconnect and start again on the new settings
void MQTT_Client::rejoin()
{
  disconnect();
  begin();
}

// HA off or a new prefix, the old prefix's entities are removed first, on the old broker
void MQTT_Client::onPrefsSaved()
{
  _root_ok = false;
  _auth_fails = 0;
#ifdef MQTTAUTODISCOVER
  if (_hass_disc[0]) {
    char disc[sizeof(_hass_disc)];
    hass_prefix(disc, sizeof(disc));
    bool gone = strcmp(disc, _hass_disc) != 0;
    if (gone && s_mq.connected()) {
      if (_hass_walk_state == HASS_PUBLISH) _hass_walk_state = HASS_IDLE;
      startHassWalk(HASS_REMOVE);
      return;
    }
    _hass_gone = gone;   // not connected, the next connect removes them
  }
#endif
  rejoin();
}
#endif
