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

AsyncMqttClient* mqttClient;

MQTT_Client& mqtt = MQTT_Client::getInstance();
EG_REGISTER_MODULE(mqtt)

static const EGPref MQTT_PREF_ITEMS[] = {
  {"server",   "Server",     "Broker address or IP",      "",               nullptr, 0, 0,     16, EGP_STRING, 0},
  {"port",     "Port",       "",                          "1883",           nullptr, 1, 65535, 0,  EGP_UINT,   0},
  {"user",     "User",       "",                          "",               nullptr, 0, 0,     32, EGP_STRING, 0},
  {"password", "Password",   "",                          "",               nullptr, 0, 0,     32, EGP_STRING, EGP_SENSITIVE},
  {"topic",    "Root Topic", "Root topic; {id}=chip ID",  "ESPGeiger-{id}", nullptr, 0, 0,     16, EGP_STRING, 0},
  {"interval", "Interval",   "Publish interval (sec)",    "60",             nullptr, MQTT_MIN_TIME, MQTT_MAX_TIME, 0, EGP_UINT, 0},
#ifdef MQTTAUTODISCOVER
  {"hass_enabled", "HA Autodiscovery",       "Publish HA autodiscovery",         MQTT_HASS_DEFAULT,    nullptr, 0, 0, 0,  EGP_BOOL,   0},
  {"hass_topic",   "HA Autodiscovery Topic", "HA discovery prefix",              MQTT_DISCOVERY_TOPIC, nullptr, 0, 0, 32, EGP_STRING, 0},
#endif
};

static const EGPrefGroup MQTT_PREF_GROUP = {
  "mqtt", "MQTT", 1,
  MQTT_PREF_ITEMS,
  sizeof(MQTT_PREF_ITEMS) / sizeof(MQTT_PREF_ITEMS[0]),
};

const EGPrefGroup* MQTT_Client::prefs_group() { return &MQTT_PREF_GROUP; }

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

  if (status.warmup) {
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
  status.led.Blink(500, 500);
  time_t currentTime = time(NULL);
  struct tm *timeinfo = gmtime(&currentTime);
  static char buffer[512];
  char dateTime[24];
  snprintf_P(dateTime, sizeof(dateTime), PSTR("%04d-%02d-%02dT%02d:%02d:%02dZ"),
    1900+timeinfo->tm_year, timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

  int pos = snprintf_P(buffer, sizeof(buffer),
    PSTR("{\"time\":\"%s\",\"ut\":%lu,\"board\":\"%s\",\"model\":\"%s\""
         ",\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"c_total\":%u"
         ",\"tick\":%u,\"t_max\":%u,\"lps\":%u"),
    dateTime, DeviceInfo::uptime(),
    DeviceInfo::chipmodel(), DeviceInfo::geigermodel(),
    Wifi::ssid, Wifi::ip, (int)Wifi::rssi,
    gcounter.total_clicks, status.tick_us, status.tick_max_us, status.lps);
#ifdef MQTT_MEM_DEBUG
  #ifdef ESP8266
    uint32_t heap_free;
    uint16_t heap_max;
    uint8_t heap_frag;
    ESP.getHeapStats(&heap_free, &heap_max, &heap_frag);
    pos += snprintf_P(buffer + pos, sizeof(buffer) - pos,
      PSTR(",\"free_mem\":%u,\"heap_max\":%u,\"heap_frag\":%u"), heap_free, heap_max, heap_frag);
  #else
    pos += snprintf_P(buffer + pos, sizeof(buffer) - pos,
      PSTR(",\"free_mem\":%u,\"min_free\":%u,\"heap_max\":%u"),
      ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
  #endif
#else
  pos += snprintf_P(buffer + pos, sizeof(buffer) - pos,
    PSTR(",\"free_mem\":%u"), ESP.getFreeHeap());
#endif
  snprintf_P(buffer + pos, sizeof(buffer) - pos, PSTR("}"));

  char topic[64];
  buildTopic(topic, sizeof(topic), "tele", topicStatus);
  uint16_t pid = mqttClient->publish(topic, 1, false, buffer);
  Log::console(PSTR("MQTT: Published"));
  status.send_indicator = 2;
  last_attempt_ms = millis();
  last_ok = (pid != 0) && connected;
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
    static char sbuf[256];
    int sp = snprintf_P(sbuf, sizeof(sbuf),
      PSTR("{\"cpm\":%s,\"usv\":%s,\"cps\":%s,\"cpm5\":%s,\"cpm15\":%s"),
      b_cpm, b_usv, b_cps, b_cpm5, b_cpm15);
#ifdef ESPGEIGER_HW
    char b_hv[12];
    format_f(b_hv, sizeof(b_hv), status.hvReading.get());
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
  format_f(valBuf, sizeof(valBuf), status.hvReading.get());
  buildTopic(topic, sizeof(topic), "stat", PSTR("HV"));
  mqttClient->publish(topic, 1, false, valBuf);
#endif
  // ---- end LEGACY block ----

  status.send_indicator = 2;
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
  snprintf(out, outsz, "%s/%s/%s", _cachedRootTopic, middle, cmd);
}
#ifdef MQTTAUTODISCOVER

struct HassSensor {
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

static const HassSensor hass_sensors[] = {
  // Main measurement sensors - extracted from tele/sensor JSON.
  {"cpm",   "CPM",        "{{ value_json.cpm }}",   "CPM",       "mdi:pulse",          "~/tele/sensor", "",                "measurement",      ""},
  {"cpm5",  "CPM5",       "{{ value_json.cpm5 }}",  "CPM",       "mdi:pulse",          "~/tele/sensor", "",                "measurement",      ""},
  {"cpm15", "CPM15",      "{{ value_json.cpm15 }}", "CPM",       "mdi:pulse",          "~/tele/sensor", "",                "measurement",      ""},
  {"usv",   "\u00B5Sv/h", "{{ value_json.usv }}",   "\u00B5S/h", "mdi:radioactive",    "~/tele/sensor", "",                "measurement",      ""},
#ifdef ESPGEIGER_HW
  {"hv",    "HV",         "{{ value_json.hv }}",    "V",         "mdi:lightning-bolt", "~/tele/sensor", "",                "measurement",      ""},
#endif
  {"c_total", "Total Clicks", "{{ value_json.c_total }}", "", "mdi:counter", "~/tele/status", "", "total_increasing", ""},
  // Diagnostic sensors - extracted from tele/status JSON.
  {"tick",     "tick",        "{{ value_json.tick }}",     "\u00B5s", "mdi:timer-outline",       "~/tele/status", "",                "measurement",      "diagnostic"},
  {"t_max",    "tick max",    "{{ value_json.t_max }}",    "\u00B5s", "mdi:timer-alert-outline", "~/tele/status", "",                "measurement",      "diagnostic"},
  {"lps",      "LPS",         "{{ value_json.lps }}",      "1/s",     "mdi:speedometer",         "~/tele/status", "",                "measurement",      "diagnostic"},
  {"rssi",     "RSSI",        "{{ value_json.rssi }}",     "dBm",     "mdi:wifi",                "~/tele/status", "signal_strength", "measurement",      "diagnostic"},
  {"uptime",   "Uptime",      "{{ value_json.ut }}",       "s",       "mdi:clock-outline",       "~/tele/status", "duration",        "total_increasing", "diagnostic"},
  {"free_mem", "Free Memory", "{{ value_json.free_mem }}", "B",       "mdi:memory",              "~/tele/status", "",                "measurement",      "diagnostic"},
};
static constexpr size_t hass_sensor_count = sizeof(hass_sensors) / sizeof(hass_sensors[0]);

struct HassBinarySensor {
  const char* id;
  const char* name;
  const char* val_tpl;
  const char* icon;
  const char* stat_t;
  const char* dev_cla;   // "problem" / "safety" / etc.
};
static const HassBinarySensor hass_binary_sensors[] = {
  {"warn",  "Warning", "{{ value_json.warn }}",  "mdi:alert-outline",          "~/tele/sensor", "problem"},
  {"alert", "Alert",   "{{ value_json.alert }}", "mdi:alert-octagram-outline", "~/tele/sensor", "safety"},
};
static constexpr size_t hass_binary_sensor_count = sizeof(hass_binary_sensors) / sizeof(hass_binary_sensors[0]);

// Shared path builder for HA discovery - avoids String heap allocations.
static int buildHassPath(char* buf, size_t sz, const char* disc,
                          const char* type, const char* name) {
  return snprintf(buf, sz, "%s/%s/%s-%s/config", disc, type, DeviceInfo::hostname(), name);
}

void MQTT_Client::setupHassCB() {
  if (!EGPrefs::getBool("mqtt", "hass_enabled")) return;
  const char* _discovery_topic = EGPrefs::getString("mqtt", "hass_topic");
  if (_discovery_topic[0] == '\0') return;
  char path[128];
  snprintf(path, sizeof(path), "%s/status", _discovery_topic);
  mqttClient->subscribe(path, 2);
}

void MQTT_Client::setupHassAuto() {
  if (!EGPrefs::getBool("mqtt", "hass_enabled")) return;
  const char* _discovery_topic = EGPrefs::getString("mqtt", "hass_topic");
  if (_discovery_topic[0] == '\0') return;

  Log::console(PSTR("MQTT: Publishing HA autodiscovery"));

  for (size_t i = 0; i < hass_sensor_count; i++) {
    const HassSensor& s = hass_sensors[i];
    HassExtra extras[4] = {
      {"stat_t",  s.stat_t},
      {"val_tpl", s.val_tpl},
      {"ic",      s.icon},
    };
    size_t n = 3;
    if (s.unit[0]) extras[n++] = {"unit_of_meas", s.unit};
    publishHassTopic("sensor", s.id, s.name,
      s.dev_cla, s.state_cla, s.ent_cat, "", extras, n);
  }

  for (size_t i = 0; i < hass_binary_sensor_count; i++) {
    const HassBinarySensor& b = hass_binary_sensors[i];
    HassExtra extras[] = {
      {"stat_t",  b.stat_t},
      {"val_tpl", b.val_tpl},
      {"pl_on",   "1"},
      {"pl_off",  "0"},
      {"ic",      b.icon},
    };
    publishHassTopic("binary_sensor", b.id, b.name,
      b.dev_cla, "", "", "", extras, 5);
  }
}

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
  static char buffer[MQTT_JSON_BUFFER_SIZE];

  int pos = snprintf_P(buffer, sizeof(buffer),
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

  if (devCla[0])
    pos += snprintf_P(buffer+pos, sizeof(buffer)-pos, PSTR(",\"dev_cla\":\"%s\""), devCla);
  if (stateCla[0])
    pos += snprintf_P(buffer+pos, sizeof(buffer)-pos, PSTR(",\"stat_cla\":\"%s\""), stateCla);
  if (entCat[0])
    pos += snprintf_P(buffer+pos, sizeof(buffer)-pos, PSTR(",\"ent_cat\":\"%s\""), entCat);
  if (cmdTopic[0])
    pos += snprintf_P(buffer+pos, sizeof(buffer)-pos, PSTR(",\"cmd_t\":\"~%s\""), cmdTopic);

  for (size_t i = 0; i < n_extras; i++) {
    if (strcmp(extras[i].value, "true") == 0)
      pos += snprintf_P(buffer+pos, sizeof(buffer)-pos, PSTR(",\"%s\":true"), extras[i].key);
    else if (strcmp(extras[i].value, "false") == 0)
      pos += snprintf_P(buffer+pos, sizeof(buffer)-pos, PSTR(",\"%s\":false"), extras[i].key);
    else
      pos += snprintf_P(buffer+pos, sizeof(buffer)-pos, PSTR(",\"%s\":\"%s\""), extras[i].key, extras[i].value);
  }

  if (pos + 1 < (int)sizeof(buffer)) { buffer[pos++] = '}'; buffer[pos] = '\0'; }

  char path[128];
  buildHassPath(path, sizeof(path), disc, type, id);
  mqttClient->publish(path, 1, false, buffer);
}

void MQTT_Client::removeHASSConfig()
{
  for (size_t i = 0; i < hass_sensor_count; i++) {
    removeHassTopic(PSTR("sensor"), hass_sensors[i].id);
  }
  for (size_t i = 0; i < hass_binary_sensor_count; i++) {
    removeHassTopic(PSTR("binary_sensor"), hass_binary_sensors[i].id);
  }
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
  snprintf(path, sizeof(path), "%s/status", _discovery_topic);

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
