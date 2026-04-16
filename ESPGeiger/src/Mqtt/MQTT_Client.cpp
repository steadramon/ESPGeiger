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
#include "ArduinoJson.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"

AsyncMqttClient* mqttClient;

MQTT_Client& mqtt = MQTT_Client::getInstance();
EG_REGISTER_MODULE(mqtt)

MQTT_Client::MQTT_Client()
{
}

void MQTT_Client::disconnect()
{
  if (!mqttClient) {
    return;
  }
  if (!status.mqtt_connected) {
    return;
  }
  mqttClient->disconnect(true);
  mqttEnabled = true;
  reconnectAttempts = 0;
  lastConnectionAttempt = 0;
  _rootTopicCached = false;
#ifdef MQTTAUTODISCOVER
  // Force HA autodiscovery republish on next connect — the user just saved
  // config, so topic/prefix/entity_category may have changed.
  _hass_last_publish = 0;
#endif
}

void MQTT_Client::onMqttConnect(bool sessionPresent) {
  Log::console(PSTR("MQTT: Connected"));
  status.mqtt_connected = true;
  reconnectAttempts = 0;
  mqttClient->publish(this->last_will_.topic.c_str(), 1, false, lwtOnline);
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
  status.mqtt_connected = false;
}

void MQTT_Client::setInterval(int interval) {
  if (interval < MQTT_MIN_TIME) {
    interval = MQTT_MIN_TIME;
  }
  if (interval > MQTT_MAX_TIME) {
    interval = MQTT_MAX_TIME;
  }
  pingInterval = interval * 1000;
}

int MQTT_Client::getInterval() {
  return (int)(pingInterval / 1000);
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

  if (!status.mqtt_connected) {
    onMqttConnect(true);
  }

  if (now - lastStatus > statusInterval)
  {
    status.led.Blink(500, 500);
    ConfigManager &configManager = ConfigManager::getInstance();
    lastStatus = now - (now % 1000);
    const size_t capacity = JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(21) + 22 + 20 + 20;
    DynamicJsonDocument doc(capacity);
    time_t currentTime = time (NULL);
    struct tm *timeinfo = localtime (&currentTime);
    static char buffer[512];
    char dateTime[20];
    snprintf_P (dateTime, sizeof (dateTime), PSTR("%04d-%02d-%02dT%02d:%02d:%02d"),
      1900+timeinfo->tm_year, timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec
    );

    doc["time"] = dateTime;
    doc["uptime"] = configManager.getUptimeString();
    doc["ut"] = configManager.getUptime();
    doc["board"] = configManager.GetChipModel();
    doc["model"] = configManager.getParamValueFromID("geigerModel");
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["c_total"] = gcounter.total_clicks;
    doc["tick"] = status.tick_us;
    doc["t_max"] = status.tick_max_us;
    doc["lps"] = status.lps;
#ifdef MQTT_MEM_DEBUG
  #ifdef ESP8266
    uint32_t heap_free;
    uint16_t heap_max;
    uint8_t heap_frag;
    ESP.getHeapStats(&heap_free, &heap_max, &heap_frag);
    doc["free_mem"] = heap_free;
    doc["heap_max"] = heap_max;
    doc["heap_frag"] = heap_frag;
  #else
    doc["free_mem"] = ESP.getFreeHeap();
    doc["min_free"] = ESP.getMinFreeHeap();
    doc["heap_max"] = ESP.getMaxAllocHeap();
  #endif
#else
    doc["free_mem"] = ESP.getFreeHeap();
#endif
    serializeJson(doc, buffer);
    doc.clear();

    char topic[64];
    buildTopic(topic, sizeof(topic), "tele", topicStatus);
    uint16_t pid = mqttClient->publish(topic, 1, false, buffer);
    Log::console(PSTR("MQTT: Published"));
    status.last_send = millis();
    last_attempt_ms = status.last_send;
    last_ok = (pid != 0) && status.mqtt_connected;
  }

  if (now - lastPing > pingInterval)
  {
    lastPing = now - (now % 1000);

    {
      const size_t cap = JSON_OBJECT_SIZE(10) + 64;
      DynamicJsonDocument sdoc(cap);
      char b_cpm[12], b_usv[12], b_cps[12], b_cpm5[12], b_cpm15[12];
      format_f(b_cpm,   sizeof(b_cpm),   gcounter.get_cpmf());
      format_f(b_usv,   sizeof(b_usv),   gcounter.get_usv());
      format_f(b_cps,   sizeof(b_cps),   gcounter.get_cps());
      format_f(b_cpm5,  sizeof(b_cpm5),  gcounter.get_cpm5f());
      format_f(b_cpm15, sizeof(b_cpm15), gcounter.get_cpm15f());
#ifdef ESPGEIGER_HW
      char b_hv[12];
      format_f(b_hv, sizeof(b_hv), status.hvReading.get());
#endif
      sdoc["cpm"]   = serialized(b_cpm);
      sdoc["usv"]   = serialized(b_usv);
      sdoc["cps"]   = serialized(b_cps);
      sdoc["cpm5"]  = serialized(b_cpm5);
      sdoc["cpm15"] = serialized(b_cpm15);
#ifdef ESPGEIGER_HW
      sdoc["hv"]    = serialized(b_hv);
#endif
      sdoc["warn"]  = gcounter.is_warning() ? 1 : 0;
      sdoc["alert"] = gcounter.is_alert() ? 1 : 0;
      static char sbuf[256];
      serializeJson(sdoc, sbuf, sizeof(sbuf));
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

    status.last_send = millis();
    last_attempt_ms = status.last_send;
    last_ok = status.mqtt_connected;
  }

  if (status.warmup) {
    return;
  }

  if (gcounter.is_warning() != warnSent) {
    warnSent = gcounter.is_warning();
    char topic[64];
    buildTopic(topic, sizeof(topic), "stat", PSTR("WARN"));
    mqttClient->publish(topic, 1, false, warnSent ? "1" : "0");
  }
  if (gcounter.is_alert() != alertSent) {
    alertSent = gcounter.is_alert();
    char topic[64];
    buildTopic(topic, sizeof(topic), "stat", PSTR("ALERT"));
    mqttClient->publish(topic, 1, false, alertSent ? "1" : "0");
  }
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

  ConfigManager &configManager = ConfigManager::getInstance();

  const char* _mqtt_server = configManager.getParamValueFromID("mqttServer");
  if (_mqtt_server == NULL) {
    mqttEnabled = false;
    return;
  }

  const char* _mqtt_port = configManager.getParamValueFromID("mqttPort");
  int mqtt_port = (_mqtt_port != NULL) ? atoi(_mqtt_port) : 1883;

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
    mqttClient->setWill(last_will_.topic.c_str(), 1, false, last_will_.payload.c_str());
  }

  mqttClient->setClientId(configManager.getHostName());
  mqttClient->setServer(_mqtt_server, mqtt_port);
  const char* _mqtt_user = configManager.getParamValueFromID("mqttUser");
  const char* _mqtt_pass = configManager.getParamValueFromID("mqttPassword");
  if (_mqtt_user != NULL && _mqtt_pass != NULL) mqttClient->setCredentials(_mqtt_user, _mqtt_pass);
  const char* _mqtt_time = configManager.getParamValueFromID("mqttTime");

  if (_mqtt_time == NULL) {
    _mqtt_time = "60";
  }

  if (atoi(_mqtt_time) != getInterval()) {
    setInterval(atoi(_mqtt_time));
    Log::console(PSTR("MQTT: Submission Interval %d seconds"), getInterval());
  }
  reconnectAttempts++;
  Log::console(PSTR("MQTT: Connecting (attempt %d) ... %s:%d"), reconnectAttempts, _mqtt_server, mqtt_port);
  mqttClient->connect();
}

void MQTT_Client::buildTopic(char* out, size_t outsz, const char* middle, const char* cmd)
{
  if (!_rootTopicCached) {
    ConfigManager &configManager = ConfigManager::getInstance();
    const char* rootTopic = configManager.getParamValueFromID("mqttTopic");
    if (rootTopic == nullptr) rootTopic = "";
    const char* chipId = configManager.getChipID();
    // Expand {id} inline. Single-pass, bounded write.
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
  // Main measurement sensors — extracted from tele/sensor JSON.
  {"cpm",   "CPM",        "{{ value_json.cpm }}",   "CPM",       "mdi:pulse",          "~/tele/sensor", "",                "measurement",      ""},
  {"cpm5",  "CPM5",       "{{ value_json.cpm5 }}",  "CPM",       "mdi:pulse",          "~/tele/sensor", "",                "measurement",      ""},
  {"cpm15", "CPM15",      "{{ value_json.cpm15 }}", "CPM",       "mdi:pulse",          "~/tele/sensor", "",                "measurement",      ""},
  {"usv",   "\u00B5Sv/h", "{{ value_json.usv }}",   "\u00B5S/h", "mdi:radioactive",    "~/tele/sensor", "",                "measurement",      ""},
#ifdef ESPGEIGER_HW
  {"hv",    "HV",         "{{ value_json.hv }}",    "V",         "mdi:lightning-bolt", "~/tele/sensor", "",                "measurement",      ""},
#endif
  {"c_total", "Total Clicks", "{{ value_json.c_total }}", "", "mdi:counter", "~/tele/status", "", "total_increasing", ""},
  // Diagnostic sensors — extracted from tele/status JSON.
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

void MQTT_Client::setupHassCB() {
  ConfigManager &configManager = ConfigManager::getInstance();
  const char* _send = configManager.getParamValueFromID("hassSend");
  if ((_send == NULL) || (strcmp(_send, "N") == 0)) {
    return;
  }
  const char* _discovery_topic = configManager.getParamValueFromID("hassDisc");
  if (_discovery_topic == NULL) {
    return;
  }
  String path = _discovery_topic;
  path.concat(PSTR("/status"));
  mqttClient->subscribe(path.c_str(), 2);
}

void MQTT_Client::setupHassAuto() {
  ConfigManager &configManager = ConfigManager::getInstance();
  const char* _send = configManager.getParamValueFromID("hassSend");
  if ((_send == NULL) || (strcmp(_send, "N") == 0)) {
    return;
  }
  const char* _discovery_topic = configManager.getParamValueFromID("hassDisc");
  if (_discovery_topic == NULL) {
    return;
  }

  Log::console(PSTR("MQTT: Publishing HA autodiscovery"));

  for (size_t i = 0; i < hass_sensor_count; i++) {
    const HassSensor& s = hass_sensors[i];
    std::vector<std::pair<const char*, const char*>> extras = {
      {"stat_t",  s.stat_t},
      {"val_tpl", s.val_tpl},
      {"ic",      s.icon},
    };
    if (s.unit[0]) extras.push_back({"unit_of_meas", s.unit});
    publishHassTopic(PSTR("sensor"),
      s.id,
      s.name,
      s.id,
      s.id,            // stateTopic — default ignored, stat_t below overrides.
      "",
      s.dev_cla,
      s.state_cla,
      s.ent_cat,
      "",
      extras);
  }

  for (size_t i = 0; i < hass_binary_sensor_count; i++) {
    const HassBinarySensor& b = hass_binary_sensors[i];
    publishHassTopic(PSTR("binary_sensor"),
      b.id,
      b.name,
      b.id,
      b.id,
      "",
      b.dev_cla,
      "",
      "",
      "",
      { {"stat_t",  b.stat_t},
        {"val_tpl", b.val_tpl},
        {"pl_on",   "1"},
        {"pl_off",  "0"},
        {"ic",      b.icon} });
  }
}

void MQTT_Client::publishHassTopic(const String& mqttDeviceType,
                               const String& mqttDeviceName,
                               const String& displayName,
                               const String& name,
                               const String& stateTopic,
                               const String& deviceType,
                               const String& deviceClass,
                               const String& stateClass,
                               const String& entityCat,
                               const String& commandTopic,
                               std::vector<std::pair<const char*, const char*>> additionalEntries
)
{
  ConfigManager &configManager = ConfigManager::getInstance();

  const char* _send = configManager.getParamValueFromID("hassSend");
  if ((_send == NULL) || (strcmp(_send, "N") == 0)) {
    return;
  }
  const char* _discovery_topic = configManager.getParamValueFromID("hassDisc");
  if (_discovery_topic == NULL) {
    return;
  }

  DynamicJsonDocument json(MQTT_JSON_BUFFER_SIZE);

  json.clear();
  auto dev = json.createNestedObject("device");
  json["device"]["mf"] = configManager.getThingName();
  json["device"]["mdl"] = configManager.GetChipModel() + String(" ") + status.geiger_model;
  json["device"]["name"] = configManager.getHostName();
  json["device"]["sw"] = status.version + String("/") + status.git_version;
  auto ids = dev.createNestedArray("identifiers");
  ids.add(configManager.getHostName());
  json["~"] = configManager.getHostName();
  json["name"] = configManager.getHostName() + String(" " + displayName);
  json["stat_t"] = PSTR("~/stat/") + stateTopic;
  json["uniq_id"] = configManager.getHostName() + String("_") + mqttDeviceName;

  if(deviceClass != "")
  {
    json["dev_cla"] = deviceClass;
  }

  if(stateClass != "")
  {
    json["stat_cla"] = stateClass;
  }
  if(entityCat != "")
  {
    json["ent_cat"] = entityCat;
  }
  if(commandTopic != "")
  {
    json["cmd_t"] = String("~") + commandTopic;
  }

  for(const auto& entry : additionalEntries) {
    if(strcmp(entry.second, "true") == 0) {
      json[entry.first] = true;
    } else if(strcmp(entry.second, "false") == 0) {
      json[entry.first] = false;
    }
    else {
      json[entry.first] = entry.second;
    }
  }

  static char buffer[MQTT_JSON_BUFFER_SIZE];
  serializeJson(json, buffer);
  json.clear();

  String path = _discovery_topic;
  path.concat(PSTR("/"));
  path.concat(mqttDeviceType);
  path.concat(PSTR("/"));
  path.concat(configManager.getHostName());
  path.concat(PSTR("-"));
  path.concat(mqttDeviceName);
  path.concat(PSTR("/config"));
  mqttClient->publish(path.c_str(), 1, false, buffer);
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

void MQTT_Client::removeHassTopic(const String& mqttDeviceType, const String& mqttDeviceName)
{
  if (!mqttClient) {
    return;
  }

  ConfigManager &configManager = ConfigManager::getInstance();

  const char* _discovery_topic = configManager.getParamValueFromID("hassDisc");
  if (_discovery_topic == NULL) {
    _discovery_topic = MQTT_DISCOVERY_TOPIC;
  }

  String path = _discovery_topic;
  path.concat(PSTR("/"));
  path.concat(mqttDeviceType);
  path.concat(PSTR("/"));
  path.concat(configManager.getHostName());
  path.concat(PSTR("/"));
  path.concat(mqttDeviceName);
  path.concat(PSTR("/config"));

  mqttClient->publish(path.c_str(), 1, false, "");
}
#endif
  
void MQTT_Client::onMqttMessage(char* topic, char* payload)
{
  ConfigManager &configManager = ConfigManager::getInstance();
#ifdef MQTTAUTODISCOVER
  const char* _discovery_topic = configManager.getParamValueFromID("hassDisc");
  if (_discovery_topic == NULL) {
    _discovery_topic = MQTT_DISCOVERY_TOPIC;
  }
  String path = _discovery_topic;
  path.concat(PSTR("/status"));

  if (strcmp(path.c_str(), topic) == 0 ) {
    const char* _send = configManager.getParamValueFromID("hassSend");
    if ((_send == NULL) || (strcmp(_send, "N") == 0)) {
      return;
    }
    if (strcmp(payload, "online") == 0) {
      Log::debug(PSTR("MQTT: HA is back online"));
      this->setupHassAuto();
    }
  }
#endif
}

void MQTT_Client::begin()
{
  ConfigManager &configManager = ConfigManager::getInstance();
  const char* _mqtt_server = configManager.getParamValueFromID("mqttServer");

  if (_mqtt_server == NULL) {
    mqttEnabled = false;
    Log::console(PSTR("MQTT: No server set"));
    return;
  }
  mqttEnabled = true;
}
#endif
