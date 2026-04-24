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
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Util/Wifi.h"
#include "../Util/TickProfile.h"
#include "../Util/StringUtil.h"
#ifdef ESPGEIGER_HW
#include "../ESPGHW/ESPGHW.h"
#endif

extern bool past_warmup;
extern uint8_t send_indicator;

AsyncMqttClient* mqttClient;

MQTT_Client& mqtt = MQTT_Client::getInstance();
EG_REGISTER_MODULE(mqtt)

// Shared publish buffer — ESP8266 only. NONOS is cooperative so
// onMqttConnect (HA discovery) and loop() (status/ping) never overlap;
// one buffer is safe. ESP32's async_tcp can preempt, so per-method
// statics there.
#ifdef ESP8266
static char s_pub_buffer[MQTT_JSON_BUFFER_SIZE];
#endif

static const EGPref MQTT_PREF_ITEMS[] = {
  {"server",   "Server",     "Broker address or IP",      "",               "[A-Za-z0-9.:_\\-]+", 0, 0, 16, EGP_STRING, 0},
  {"port",     "Port",       "",                          "1883",           nullptr, 1, 65535, 0,  EGP_UINT,   0},
  {"user",     "User",       "",                          "",               nullptr, 0, 0,     32, EGP_STRING, 0},
  {"password", "Password",   "",                          "",               nullptr, 0, 0,     32, EGP_STRING, EGP_SENSITIVE},
  {"topic",    "Root Topic", "Root topic; {id}=chip ID",  "ESPGeiger-{id}", "[A-Za-z0-9_\\/\\{\\}\\-]+", 0, 0, 16, EGP_STRING, 0},
  {"interval", "Interval",   "Publish interval (sec)",    "60",             nullptr, MQTT_MIN_TIME, MQTT_MAX_TIME, 0, EGP_UINT, 0},
#ifdef MQTTAUTODISCOVER
  {"hass_enabled", "HA Autodiscovery",       "Publish HA autodiscovery",         MQTT_HASS_DEFAULT,    nullptr, 0, 0, 0,  EGP_BOOL,   0},
  {"hass_topic",   "HA Autodiscovery Topic", "HA discovery prefix",              MQTT_DISCOVERY_TOPIC, "[A-Za-z0-9_\\/\\-]+", 0, 0, 32, EGP_STRING, 0},
#endif
};

static const EGPrefGroup MQTT_PREF_GROUP = {
  "mqtt", "MQTT", 1,
  MQTT_PREF_ITEMS,
  sizeof(MQTT_PREF_ITEMS) / sizeof(MQTT_PREF_ITEMS[0]),
};

const EGPrefGroup* MQTT_Client::prefs_group() { return &MQTT_PREF_GROUP; }

size_t MQTT_Client::status_json(char* buf, size_t cap, unsigned long now) {
  if (EGPrefs::getString("mqtt", "server")[0] == '\0') return 0;
  return write_status_json(buf, cap, "mqtt", last_ok && connected, last_attempt_ms, now);
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias MQTT_LEGACY[] = {
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
  {nullptr, nullptr},
};
const EGLegacyAlias* MQTT_Client::legacy_aliases() { return MQTT_LEGACY; }
// === END LEGACY IMPORT ===

MQTT_Client::MQTT_Client()
{
}

void MQTT_Client::disconnect()
{
  if (!mqttClient) {
    return;
  }
  if (!connected) {
    return;
  }
  mqttClient->disconnect(true);
  mqttEnabled = true;
  reconnectAttempts = 0;
  lastConnectionAttempt = 0;
  _rootTopicCached = false;
#ifdef MQTTAUTODISCOVER
  _hass_last_publish = 0;  // force HA rediscovery on next connect
#endif
}

void MQTT_Client::onMqttConnect(bool sessionPresent) {
  Log::console(PSTR("MQTT: Connected"));
  connected = true;
  reconnectAttempts = 0;
  mqttClient->publish(this->last_will_.topic.c_str(), 1, true, lwtOnline);
#ifdef MQTTAUTODISCOVER
  unsigned long now = millis();
  const unsigned long refresh_ms = MQTT_HASS_REFRESH_S * 1000UL;
  bool refresh_due = (_hass_last_publish == 0) ||
                     (now - _hass_last_publish >= refresh_ms);
  if (refresh_due) {
    this->setupHassAuto();
    _hass_last_publish = now;
  }
  this->setupHassCB();
#endif
}

void MQTT_Client::onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  const char* text;
  switch (reason) {
  case AsyncMqttClientDisconnectReason::TCP_DISCONNECTED:
     text = PSTR("TCP_DISCONNECTED"); break;
  case AsyncMqttClientDisconnectReason::MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
     text = PSTR("MQTT_UNACCEPTABLE_PROTOCOL_VERSION"); break;
  case AsyncMqttClientDisconnectReason::MQTT_IDENTIFIER_REJECTED:
     text = PSTR("MQTT_IDENTIFIER_REJECTED"); break;
  case AsyncMqttClientDisconnectReason::MQTT_SERVER_UNAVAILABLE:
     text = PSTR("MQTT_SERVER_UNAVAILABLE"); break;
  case AsyncMqttClientDisconnectReason::MQTT_MALFORMED_CREDENTIALS:
     text = PSTR("MQTT_MALFORMED_CREDENTIALS"); break;
  case AsyncMqttClientDisconnectReason::MQTT_NOT_AUTHORIZED:
     text = PSTR("MQTT_NOT_AUTHORIZED"); break;
  default:
     text = PSTR("UNKNOWN"); break;
  }
  Log::console(PSTR("MQTT: Disconnected (%s)"), text);
  lastConnectionAttempt = millis();
  connected = false;
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

void MQTT_Client::s_tick(unsigned long now)
{
  if (!mqttEnabled) {
    return;
  }

  if (!mqttClient || !mqttClient->connected()) {
    reconnect();
    return;
  }

  if (!connected) {
    onMqttConnect(true);
  }

  // Advance by exact interval; snap to now if we fell more than one behind.
  if ((now - lastStatus) > statusInterval) {
    lastStatus += statusInterval;
    if ((now - lastStatus) > statusInterval) lastStatus = now;
    _pending |= PEND_STATUS;
    // First-fire: stagger ping half an interval so their bursts don't stack.
    if (lastPing == 0) {
      lastPing = now - (pingInterval / 2);
    }
  }

  if ((now - lastPing) > pingInterval) {
    lastPing += pingInterval;
    if ((now - lastPing) > pingInterval) lastPing = now;
    _pending |= PEND_PING;
  }

  if (!past_warmup) {
    return;
  }

  if (gcounter.is_warning() != warnSent) {
    warnSent = gcounter.is_warning();
    _pending |= PEND_WARN;
  }
  if (gcounter.is_alert() != alertSent) {
    alertSent = gcounter.is_alert();
    _pending |= PEND_ALERT;
  }
}

void MQTT_Client::loop(unsigned long /*now*/)
{
  if (!mqttEnabled) return;
  if (!mqttClient || !mqttClient->connected()) return;
  // One drain per call - prioritise alarm latency over throughput.
  if (_pending & PEND_WARN)       { _pending &= ~PEND_WARN;   publishWarn();   return; }
  if (_pending & PEND_ALERT)      { _pending &= ~PEND_ALERT;  publishAlert();  return; }
  if (_pending & PEND_STATUS)     { _pending &= ~PEND_STATUS; publishStatus(); return; }
  if (_pending & PEND_PING)       { _pending &= ~PEND_PING;   publishPing(); }
}

void MQTT_Client::publishWarn()
{
  char topic[64];
  buildTopic(topic, sizeof(topic), "stat", PSTR("WARN"));
  mqttClient->publish(topic, 1, false, warnSent ? "1" : "0");
}

void MQTT_Client::publishAlert()
{
  char topic[64];
  buildTopic(topic, sizeof(topic), "stat", PSTR("ALERT"));
  mqttClient->publish(topic, 1, false, alertSent ? "1" : "0");
}

void MQTT_Client::publishStatus()
{
  led.Blink(500, 500);
  time_t currentTime = time(NULL);
  struct tm *timeinfo = gmtime(&currentTime);
#ifdef ESP8266
  auto& buffer = s_pub_buffer;
#else
  static char buffer[512];
#endif
  char dateTime[24];
  snprintf_P(dateTime, sizeof(dateTime), PSTR("%04d-%02d-%02dT%02d:%02d:%02dZ"),
    1900+timeinfo->tm_year, timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

  size_t pos = 0;
  int n;
  n = snprintf_P(buffer, sizeof(buffer),
    PSTR("{\"time\":\"%s\",\"ut\":%lu,\"board\":\"%s\",\"model\":\"%s\""
         ",\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"c_total\":%u"
         ",\"tick\":%u,\"t_max\":%u,\"lps\":%u"),
    dateTime, DeviceInfo::uptime(),
    DeviceInfo::chipmodel(), DeviceInfo::geigermodel(),
    Wifi::ssid, Wifi::ip, (int)Wifi::rssi,
    gcounter.total_clicks, TickProfile::tick_us, TickProfile::tick_max_us, TickProfile::lps);
  advance_pos(pos, n, sizeof(buffer));
#ifdef MQTT_MEM_DEBUG
  #ifdef ESP8266
    uint32_t heap_free;
    uint16_t heap_max;
    uint8_t heap_frag;
    ESP.getHeapStats(&heap_free, &heap_max, &heap_frag);
    n = snprintf_P(buffer + pos, sizeof(buffer) - pos,
      PSTR(",\"free_mem\":%u,\"heap_max\":%u,\"heap_frag\":%u"), heap_free, heap_max, heap_frag);
    advance_pos(pos, n, sizeof(buffer));
  #else
    n = snprintf_P(buffer + pos, sizeof(buffer) - pos,
      PSTR(",\"free_mem\":%u,\"min_free\":%u,\"heap_max\":%u"),
      ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    advance_pos(pos, n, sizeof(buffer));
  #endif
#else
  n = snprintf_P(buffer + pos, sizeof(buffer) - pos,
    PSTR(",\"free_mem\":%u"), ESP.getFreeHeap());
  advance_pos(pos, n, sizeof(buffer));
#endif
#if GEIGER_IS_SERIAL(GEIGER_TYPE) && !GEIGER_IS_TEST(GEIGER_TYPE)
  n = snprintf_P(buffer + pos, sizeof(buffer) - pos,
    PSTR(",\"ser_ok\":%d"), gcounter.is_healthy() ? 1 : 0);
  advance_pos(pos, n, sizeof(buffer));
#endif
  n = snprintf_P(buffer + pos, sizeof(buffer) - pos, PSTR("}"));
  advance_pos(pos, n, sizeof(buffer));
  buffer[pos] = '\0';

  char topic[64];
  buildTopic(topic, sizeof(topic), "tele", topicStatus);
  uint16_t pid = mqttClient->publish(topic, 1, false, buffer);
  Log::debug(PSTR("MQTT: Published"));
  send_indicator = 2;
  last_attempt_ms = millis();
  last_ok = (pid != 0) && connected;
  note_publish(last_ok);
}

void MQTT_Client::publishPing()
{
  {
    char b_cpm[12], b_usv[12], b_cps[12], b_cpm5[12], b_cpm15[12];
    format_f(b_cpm,   sizeof(b_cpm),   gcounter.get_cpmf());
    format_f(b_usv,   sizeof(b_usv),   gcounter.get_usv());
    format_f(b_cps,   sizeof(b_cps),   gcounter.get_cps());
    format_f(b_cpm5,  sizeof(b_cpm5),  gcounter.get_cpm5f());
    format_f(b_cpm15, sizeof(b_cpm15), gcounter.get_cpm15f());
#ifdef ESP8266
    auto& sbuf = s_pub_buffer;
#else
    static char sbuf[256];
#endif
    int sp = snprintf_P(sbuf, sizeof(sbuf),
      PSTR("{\"cpm\":%s,\"usv\":%s,\"cps\":%s,\"cpm5\":%s,\"cpm15\":%s"),
      b_cpm, b_usv, b_cps, b_cpm5, b_cpm15);
#ifdef ESPGEIGER_HW
    char b_hv[12];
    format_f(b_hv, sizeof(b_hv), hardware.hvReading.get());
    sp += snprintf_P(sbuf + sp, sizeof(sbuf) - sp, PSTR(",\"hv\":%s"), b_hv);
#endif
    sp += snprintf_P(sbuf + sp, sizeof(sbuf) - sp,
      PSTR(",\"warn\":%d,\"alert\":%d}"),
      gcounter.is_warning() ? 1 : 0, gcounter.is_alert() ? 1 : 0);
    char topic[64];
    buildTopic(topic, sizeof(topic), "tele", "sensor");
    mqttClient->publish(topic, 1, false, sbuf);
  }

  // ---- LEGACY ----
  char valBuf[16];
  char topic[64];

  format_f(valBuf, sizeof(valBuf), gcounter.get_cpmf());
  buildTopic(topic, sizeof(topic), "stat", PSTR("CPM"));
  mqttClient->publish(topic, 1, false, valBuf);

  format_f(valBuf, sizeof(valBuf), gcounter.get_usv());
  buildTopic(topic, sizeof(topic), "stat", PSTR("uSv"));
  mqttClient->publish(topic, 1, false, valBuf);

#ifndef DISABLE_MQTT_CPS
  format_f(valBuf, sizeof(valBuf), gcounter.get_cps());
  buildTopic(topic, sizeof(topic), "stat", PSTR("CPS"));
  mqttClient->publish(topic, 1, false, valBuf);
#endif

  format_f(valBuf, sizeof(valBuf), gcounter.get_cpm5f());
  buildTopic(topic, sizeof(topic), "stat", PSTR("CPM5"));
  mqttClient->publish(topic, 1, false, valBuf);

  format_f(valBuf, sizeof(valBuf), gcounter.get_cpm15f());
  buildTopic(topic, sizeof(topic), "stat", PSTR("CPM15"));
  mqttClient->publish(topic, 1, false, valBuf);

#ifdef ESPGEIGER_HW
  format_f(valBuf, sizeof(valBuf), hardware.hvReading.get());
  buildTopic(topic, sizeof(topic), "stat", PSTR("HV"));
  mqttClient->publish(topic, 1, false, valBuf);
#endif
  // ---- end LEGACY block ----

  send_indicator = 2;
  last_attempt_ms = millis();
  last_ok = connected;
}

void MQTT_Client::reconnect()
{
  if (mqttClient && mqttClient->connected()) {
    reconnectAttempts = 0;
    return;
  }

  // Exponential backoff: 10s, 20s, 40s, 80s, 160s, capped at 300s
  unsigned long backoff = 10000UL << min(reconnectAttempts, (uint8_t)5);
  if (backoff > 300000UL) backoff = 300000UL;

  unsigned long now = millis();
  if (lastConnectionAttempt && now - lastConnectionAttempt < backoff) {
    return;
  }
  lastConnectionAttempt = now;


  const char* _mqtt_server = EGPrefs::getString("mqtt", "server");
  if (_mqtt_server[0] == '\0') {
    mqttEnabled = false;
    return;
  }

  int mqtt_port = (int)EGPrefs::getUInt("mqtt", "port");
  if (mqtt_port <= 0) mqtt_port = 1883;

  if (mqttClient == nullptr) {
    char lwt_topic[64];
    buildTopic(lwt_topic, sizeof(lwt_topic), "tele", topicLWT);
    this->last_will_.topic = lwt_topic;
    this->last_will_.payload = lwtOffline;

    mqttClient = new AsyncMqttClient();
    mqttClient->onConnect([this] (bool sessionPresent) {
      onMqttConnect(sessionPresent);
    });
    mqttClient->onMessage([this] (char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
      onMqttMessage(topic, payload);
    });
    mqttClient->onDisconnect([this] (AsyncMqttClientDisconnectReason reason) {
      onMqttDisconnect(reason);
    });
    mqttClient->setWill(last_will_.topic.c_str(), 1, true, last_will_.payload.c_str());
  }

  mqttClient->setClientId(DeviceInfo::hostname());
  mqttClient->setServer(_mqtt_server, mqtt_port);
  const char* _mqtt_user = EGPrefs::getString("mqtt", "user");
  const char* _mqtt_pass = EGPrefs::getString("mqtt", "password");
  if (_mqtt_user[0] != '\0') mqttClient->setCredentials(_mqtt_user, _mqtt_pass);

  int mqtt_time = (int)EGPrefs::getUInt("mqtt", "interval");
  if (mqtt_time <= 0) mqtt_time = 60;
  if (mqtt_time != getInterval()) {
    setInterval(mqtt_time);
    Log::console(PSTR("MQTT: Submission Interval %d seconds"), getInterval());
  }
  reconnectAttempts++;
  Log::console(PSTR("MQTT: Connecting (attempt %d) ... %s:%d"), reconnectAttempts, _mqtt_server, mqtt_port);
  mqttClient->connect();
}

void MQTT_Client::buildTopic(char* out, size_t outsz, const char* middle, const char* cmd)
{
  if (!_rootTopicCached) {
    const char* rootTopic = EGPrefs::getString("mqtt", "topic");
    const char* chipId = DeviceInfo::chipid();
    size_t w = 0;
    const size_t cap = sizeof(_cachedRootTopic) - 1;
    for (const char* p = rootTopic; *p && w < cap; ) {
      if (strncmp(p, "{id}", 4) == 0) {
        size_t n = strlen(chipId);
        if (w + n > cap) n = cap - w;
        memcpy(_cachedRootTopic + w, chipId, n);
        w += n;
        p += 4;
      } else {
        _cachedRootTopic[w++] = *p++;
      }
    }
    _cachedRootTopic[w] = '\0';
    _rootTopicCached = true;
  }
  snprintf_P(out, outsz, PSTR("%s/%s/%s"), _cachedRootTopic, middle, cmd);
}
#ifdef MQTTAUTODISCOVER

// HA autodiscovery palette. PROGMEM puts these in flash (ESP8266's
// .rodata is SRAM otherwise). Per-sensor strings are inlined via PSTR()
// in the forEach* walkers below; this file-scope block holds only the
// categorical values reused across rows.

// Topic suffix.
static const char H_ST_SENSOR[] PROGMEM = "~/tele/sensor";
static const char H_ST_STATUS[] PROGMEM = "~/tele/status";
// state_class.
static const char H_SC_MEAS[]   PROGMEM = "measurement";
static const char H_SC_TOTINC[] PROGMEM = "total_increasing";
// device_class.
static const char H_DC_SIG[]    PROGMEM = "signal_strength";
static const char H_DC_DUR[]    PROGMEM = "duration";
static const char H_DC_PROB[]   PROGMEM = "problem";
static const char H_DC_SAFETY[] PROGMEM = "safety";
static const char H_DC_CONN[]   PROGMEM = "connectivity";
// entity_category.
static const char H_EC_DIAG[]   PROGMEM = "diagnostic";
// "not set" sentinel — consumers check pgm_read_byte(x) to skip.
static const char H_EMPTY[]     PROGMEM = "";

// Discovery type (topic-path component).
static const char H_TYPE_SENSOR[]  PROGMEM = "sensor";
static const char H_TYPE_BINSENS[] PROGMEM = "binary_sensor";

// HassExtra keys + stock pl_on / pl_off values.
static const char H_K_STAT[]  PROGMEM = "stat_t";
static const char H_K_VT[]    PROGMEM = "val_tpl";
static const char H_K_IC[]    PROGMEM = "ic";
static const char H_K_UOM[]   PROGMEM = "unit_of_meas";
static const char H_K_PLON[]  PROGMEM = "pl_on";
static const char H_K_PLOFF[] PROGMEM = "pl_off";
static const char H_V_ONE[]   PROGMEM = "1";
static const char H_V_ZERO[]  PROGMEM = "0";

static int buildHassPath(char* buf, size_t sz, const char* disc,
                          const char* type, const char* name) {
  return snprintf_P(buf, sz, PSTR("%s/%s/%s-%s/config"), disc, type, DeviceInfo::hostname(), name);
}

// Adding a sensor = one S(...) line. PSTR() only works in function
// scope, so the catalog lives here rather than a file-scope array.
// jKey is the tele/* JSON field — usually matches id (uptime→"ut" is
// the sole oddity).
void MQTT_Client::forEachHassSensor(HassSensorFn fn) {
  #define S(id_, jKey_, name_, unit_, icon_, stat_, dev_, state_, ent_) do { \
    HassSensorRow r = {PSTR(id_), PSTR(name_), PSTR("{{ value_json." jKey_ " }}"), \
                        PSTR(unit_), PSTR(icon_), stat_, dev_, state_, ent_}; \
    (this->*fn)(r); \
  } while (0)

  // tele/sensor — main measurement.
  S("cpm",      "cpm",      "CPM",          "CPM",       "mdi:pulse",          H_ST_SENSOR, H_EMPTY,  H_SC_MEAS,   H_EMPTY);
  S("cpm5",     "cpm5",     "CPM5",         "CPM",       "mdi:pulse",          H_ST_SENSOR, H_EMPTY,  H_SC_MEAS,   H_EMPTY);
  S("cpm15",    "cpm15",    "CPM15",        "CPM",       "mdi:pulse",          H_ST_SENSOR, H_EMPTY,  H_SC_MEAS,   H_EMPTY);
  S("usv",      "usv",      "\u00B5Sv/h",   "\u00B5S/h", "mdi:radioactive",    H_ST_SENSOR, H_EMPTY,  H_SC_MEAS,   H_EMPTY);
#ifdef ESPGEIGER_HW
  S("hv",       "hv",       "HV",           "V",         "mdi:lightning-bolt", H_ST_SENSOR, H_EMPTY,  H_SC_MEAS,   H_EMPTY);
#endif
  S("c_total",  "c_total",  "Total Clicks", "",          "mdi:counter",        H_ST_STATUS, H_EMPTY,  H_SC_TOTINC, H_EMPTY);
  // tele/status — diagnostic.
  S("tick",     "tick",     "tick",         "\u00B5s",   "mdi:timer-outline",  H_ST_STATUS, H_EMPTY,  H_SC_MEAS,   H_EC_DIAG);
  S("t_max",    "t_max",    "tick max",     "\u00B5s",   "mdi:timer-alert-outline", H_ST_STATUS, H_EMPTY, H_SC_MEAS, H_EC_DIAG);
  S("lps",      "lps",      "LPS",          "1/s",       "mdi:speedometer",    H_ST_STATUS, H_EMPTY,  H_SC_MEAS,   H_EC_DIAG);
  S("rssi",     "rssi",     "RSSI",         "dBm",       "mdi:wifi",           H_ST_STATUS, H_DC_SIG, H_SC_MEAS,   H_EC_DIAG);
  S("uptime",   "ut",       "Uptime",       "s",         "mdi:clock-outline",  H_ST_STATUS, H_DC_DUR, H_SC_TOTINC, H_EC_DIAG);
  S("free_mem", "free_mem", "Free Memory",  "B",         "mdi:memory",         H_ST_STATUS, H_EMPTY,  H_SC_MEAS,   H_EC_DIAG);

  #undef S
}

void MQTT_Client::forEachHassBinarySensor(HassBinaryFn fn) {
  #define B(id_, name_, icon_, stat_, dev_) do { \
    HassBinaryRow r = {PSTR(id_), PSTR(name_), PSTR("{{ value_json." id_ " }}"), \
                        PSTR(icon_), stat_, dev_}; \
    (this->*fn)(r); \
  } while (0)

  B("warn",  "Warning", "mdi:alert-outline",          H_ST_SENSOR, H_DC_PROB);
  B("alert", "Alert",   "mdi:alert-octagram-outline", H_ST_SENSOR, H_DC_SAFETY);
#if GEIGER_IS_SERIAL(GEIGER_TYPE) && !GEIGER_IS_TEST(GEIGER_TYPE)
  // Serial builds only — pulse builds have no external peer to track.
  B("ser_ok", "Serial Connected", "mdi:serial-port", H_ST_STATUS, H_DC_CONN);
#endif

  #undef B
}

void MQTT_Client::setupHassCB() {
  if (!EGPrefs::getBool("mqtt", "hass_enabled")) return;
  const char* _discovery_topic = EGPrefs::getString("mqtt", "hass_topic");
  if (_discovery_topic[0] == '\0') return;
  char path[128];
  snprintf_P(path, sizeof(path), PSTR("%s/status"), _discovery_topic);
  mqttClient->subscribe(path, 2);
}

void MQTT_Client::setupHassAuto() {
  if (!EGPrefs::getBool("mqtt", "hass_enabled")) return;
  const char* _discovery_topic = EGPrefs::getString("mqtt", "hass_topic");
  if (_discovery_topic[0] == '\0') return;

  Log::console(PSTR("MQTT: Publishing HA autodiscovery"));

  forEachHassSensor(&MQTT_Client::hassPublishSensor);
  forEachHassBinarySensor(&MQTT_Client::hassPublishBinarySensor);
}

// Per-row emitters. Instance methods so the forEach walkers can
// dispatch to either publish or remove over the same row list.
void MQTT_Client::hassPublishSensor(const HassSensorRow& r) {
  HassExtra extras[4] = {
    {H_K_STAT, r.stat_t},
    {H_K_VT,   r.val_tpl},
    {H_K_IC,   r.icon},
  };
  size_t n = 3;
  if (pgm_read_byte(r.unit)) extras[n++] = {H_K_UOM, r.unit};
  publishHassTopic(H_TYPE_SENSOR, r.id, r.name,
    r.dev_cla, r.state_cla, r.ent_cat, H_EMPTY, extras, n);
}

void MQTT_Client::hassPublishBinarySensor(const HassBinaryRow& r) {
  HassExtra extras[] = {
    {H_K_STAT,  r.stat_t},
    {H_K_VT,    r.val_tpl},
    {H_K_PLON,  H_V_ONE},
    {H_K_PLOFF, H_V_ZERO},
    {H_K_IC,    r.icon},
  };
  publishHassTopic(H_TYPE_BINSENS, r.id, r.name,
    r.dev_cla, H_EMPTY, H_EMPTY, H_EMPTY, extras, 5);
}

void MQTT_Client::hassRemoveSensor(const HassSensorRow& r) {
  removeHassTopic(H_TYPE_SENSOR, r.id);
}

void MQTT_Client::hassRemoveBinarySensor(const HassBinaryRow& r) {
  removeHassTopic(H_TYPE_BINSENS, r.id);
}

// All string args (type/id/displayName/devCla/stateCla/entCat/cmdTopic
// and extras[].key/value) are PGM_P — passed through to %s (safe on
// ESP8266 via the unaligned-load handler) with pgm_read_byte for
// "is set" checks.
void MQTT_Client::publishHassTopic(
    const char* type, const char* id, const char* displayName,
    const char* devCla, const char* stateCla, const char* entCat,
    const char* cmdTopic,
    const HassExtra* extras, size_t n_extras)
{
  if (!EGPrefs::getBool("mqtt", "hass_enabled")) return;
  const char* disc = EGPrefs::getString("mqtt", "hass_topic");
  if (disc[0] == '\0') return;

  const char* host = DeviceInfo::hostname();
#ifdef ESP8266
  // Array-ref so sizeof(buffer) still resolves to MQTT_JSON_BUFFER_SIZE.
  auto& buffer = s_pub_buffer;
#else
  // ESP32: async_tcp can preempt loop(), so don't share the buffer.
  static char buffer[MQTT_JSON_BUFFER_SIZE];
#endif

  size_t pos = 0;
  int n;
  n = snprintf_P(buffer, sizeof(buffer),
    PSTR("{\"device\":{\"mf\":\"%s\",\"mdl\":\"%s\",\"mdl_id\":\"%s\",\"name\":\"%s\""
         ",\"sw\":\"%s/%s\",\"sn\":\"%s\""
         ",\"identifiers\":[\"%s\"]"
         ",\"connections\":[[\"mac\",\"%s\"]]"
         ",\"cu\":\"http://%s/\"}"
         ",\"avty_t\":\"~/tele/lwt\",\"pl_avail\":\"Online\",\"pl_not_avail\":\"Offline\""
         ",\"~\":\"%s\",\"name\":\"%s %s\",\"uniq_id\":\"%s_%s\""),
    THING_NAME, DeviceInfo::chipmodel(), BUILD_ENV,
    host, RELEASE_VERSION, GIT_VERSION, DeviceInfo::chipid(),
    host,
    DeviceInfo::mac(),
    Wifi::ip,
    host, host, displayName, host, id);
  advance_pos(pos, n, sizeof(buffer));

  if (pgm_read_byte(devCla)) {
    n = snprintf_P(buffer+pos, sizeof(buffer)-pos, PSTR(",\"dev_cla\":\"%s\""), devCla);
    advance_pos(pos, n, sizeof(buffer));
  }
  if (pgm_read_byte(stateCla)) {
    n = snprintf_P(buffer+pos, sizeof(buffer)-pos, PSTR(",\"stat_cla\":\"%s\""), stateCla);
    advance_pos(pos, n, sizeof(buffer));
  }
  if (pgm_read_byte(entCat)) {
    n = snprintf_P(buffer+pos, sizeof(buffer)-pos, PSTR(",\"ent_cat\":\"%s\""), entCat);
    advance_pos(pos, n, sizeof(buffer));
  }
  if (pgm_read_byte(cmdTopic)) {
    n = snprintf_P(buffer+pos, sizeof(buffer)-pos, PSTR(",\"cmd_t\":\"~%s\""), cmdTopic);
    advance_pos(pos, n, sizeof(buffer));
  }

  for (size_t i = 0; i < n_extras; i++) {
    // Quoted-string form only — no caller passes bare-boolean values,
    // and strcmp_P with a flash first arg crashes Exception 3.
    n = snprintf_P(buffer+pos, sizeof(buffer)-pos, PSTR(",\"%s\":\"%s\""), extras[i].key, extras[i].value);
    advance_pos(pos, n, sizeof(buffer));
  }

  if (pos + 1 < sizeof(buffer)) { buffer[pos++] = '}'; buffer[pos] = '\0'; }

  char path[128];
  buildHassPath(path, sizeof(path), disc, type, id);
  mqttClient->publish(path, 1, false, buffer);
}

void MQTT_Client::removeHASSConfig()
{
  forEachHassSensor(&MQTT_Client::hassRemoveSensor);
  forEachHassBinarySensor(&MQTT_Client::hassRemoveBinarySensor);
}

void MQTT_Client::removeHassTopic(const char* type, const char* id)
{
  if (!mqttClient) return;
  const char* disc = EGPrefs::getString("mqtt", "hass_topic");
  if (disc[0] == '\0') disc = MQTT_DISCOVERY_TOPIC;
  char path[128];
  buildHassPath(path, sizeof(path), disc, type, id);
  mqttClient->publish(path, 1, false, "");
}
#endif

void MQTT_Client::onMqttMessage(char* topic, char* payload)
{
#ifdef MQTTAUTODISCOVER
  const char* _discovery_topic = EGPrefs::getString("mqtt", "hass_topic");
  if (_discovery_topic[0] == '\0') _discovery_topic = MQTT_DISCOVERY_TOPIC;
  char path[128];
  snprintf_P(path, sizeof(path), PSTR("%s/status"), _discovery_topic);

  if (strcmp(path, topic) == 0 ) {
    if (!EGPrefs::getBool("mqtt", "hass_enabled")) return;
    if (strcmp(payload, "online") == 0) {
      Log::debug(PSTR("MQTT: HA is back online"));
      this->setupHassAuto();
    }
  }
#endif
}

void MQTT_Client::begin()
{
  const char* _mqtt_server = EGPrefs::getString("mqtt", "server");
  if (_mqtt_server[0] == '\0') {
    mqttEnabled = false;
    Log::console(PSTR("MQTT: No server set"));
    return;
  }
  mqttEnabled = true;
}

void MQTT_Client::on_prefs_saved()
{
  _rootTopicCached = false;  // topic may have changed - force rebuild
#ifdef MQTTAUTODISCOVER
  if (connected && !EGPrefs::getBool("mqtt", "hass_enabled")) {
    removeHASSConfig();
  }
#endif
  disconnect();
  reconnectAttempts = 0;
  lastConnectionAttempt = 0;  // skip backoff - reconnect immediately
  begin();
}
#endif
