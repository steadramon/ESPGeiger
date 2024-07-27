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

AsyncMqttClient* mqttClient;

MQTT_Client::MQTT_Client()
{
}

void MQTT_Client::disconnect()
{
  mqttClient->disconnect(true);
  mqttEnabled = true;
  lastConnectionAtempt = millis() - 50000;
}

void MQTT_Client::onMqttConnect(bool sessionPresent) {
  Log::console(PSTR("MQTT: Connected"));
  status.mqtt_connected = true;
  mqttClient->publish(this->last_will_.topic.c_str(), 1, false, lwtOnline);
  this->setupHassAuto();
  this->setupHassCB();
}

void MQTT_Client::onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Log::console(PSTR("MQTT: Disconnected"));
  mqttClient->clearQueue();
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

void MQTT_Client::loop(unsigned long now)
{
  if (!mqttEnabled) {
    return;
  }

  if (!status.mqtt_connected) {
    reconnect();
    return;
  }

  if (now - lastStatus > statusInterval)
  {
    status.led.Blink(500, 500);
    ConfigManager &configManager = ConfigManager::getInstance();
    lastStatus = now - (now % 1000);
    const size_t capacity = JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(17) + 22 + 20 + 20;
    DynamicJsonDocument doc(capacity);
    time_t currentTime = time (NULL);
    struct tm *timeinfo = localtime (&currentTime);
    char buffer[1048];
    char dateTime[20];
    snprintf_P (dateTime, sizeof (dateTime), PSTR("%04d-%02d-%02dT%02d:%02d:%02d"),
      1900+timeinfo->tm_year, timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec
    );

    doc["time"] = dateTime;
    doc["uptime"] = configManager.getUptimeString();
    doc["board"] = configManager.GetChipModel();
    doc["model"] = configManager.getParamValueFromID("geigerModel");
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
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
    Log::console(PSTR("MQTT: %s"), buffer);

    mqttClient->publish(buildTopic(teleTopic, topicStatus).c_str(), 1, false, buffer);
    status.last_send = now;
  }

  if (now - lastPing > pingInterval)
  {
    lastPing = now - (now % 1000);

    mqttClient->publish(buildTopic(statTopic, PSTR("Clicks")).c_str(), 1, false, String(gcounter.total_clicks).c_str());

    mqttClient->publish(buildTopic(statTopic, PSTR("CPM")).c_str(), 1, false, String(gcounter.get_cpmf()).c_str());

    mqttClient->publish(buildTopic(statTopic, PSTR("uSv")).c_str(), 1, false, String(gcounter.get_usv()).c_str());

#ifndef DISABLE_MQTT_CPS
    mqttClient->publish(buildTopic(statTopic, PSTR("CPS")).c_str(), 1, false, String(gcounter.get_cps()).c_str());
#endif

    mqttClient->publish(buildTopic(statTopic, PSTR("CPM5")).c_str(), 1, false, String(gcounter.get_cpm5f()).c_str());

    mqttClient->publish(buildTopic(statTopic, PSTR("CPM15")).c_str(), 1, false, String(gcounter.get_cpm15f()).c_str());

#ifdef ESPGEIGER_HW
    mqttClient->publish(buildTopic(statTopic, PSTR("HV")).c_str(), 1, false, String(status.hvReading.get()).c_str());
#endif

    status.last_send = now;
  }
}

void MQTT_Client::reconnect()
{
  if (mqttClient && mqttClient->connected()) {
    return;
  }

  if (lastConnectionAtempt && millis() - lastConnectionAtempt < reconnectionInterval) {
    return;
  }

  lastConnectionAtempt = millis();

  ConfigManager &configManager = ConfigManager::getInstance();

  if (configManager.getParamValueFromID("mqttServer") == NULL)
  {
    mqttEnabled = false;
    return;
  }

  if (mqttClient == nullptr) {
    this->last_will_.topic = buildTopic(teleTopic, topicLWT);
    this->last_will_.payload = lwtOffline;

    mqttClient = new AsyncMqttClient();
    mqttClient->setClientId(configManager.getHostName());
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

  mqttClient->setServer(configManager.getParamValueFromID("mqttServer"), atoi(configManager.getParamValueFromID("mqttPort")));
  const char* _mqtt_user = configManager.getParamValueFromID("mqttUser");
  const char* _mqtt_pass = configManager.getParamValueFromID("mqttPassword");
  if (_mqtt_user[0] && _mqtt_pass[0]) mqttClient->setCredentials(_mqtt_user, _mqtt_pass);
  const char* _mqtt_time = configManager.getParamValueFromID("mqttTime");

  if (_mqtt_time == NULL) {
    _mqtt_time = "60";
  }

  setInterval(atoi(_mqtt_time));
  Log::console(PSTR("MQTT: Submission Interval %d seconds"), getInterval());
  Log::console(PSTR("MQTT: Connecting ... %s:%s"), configManager.getParamValueFromID("mqttServer"), configManager.getParamValueFromID("mqttPort"));
  mqttClient->connect();
}

String MQTT_Client::buildTopic(const char *baseTopic, const char *cmd)
{
  ConfigManager &configManager = ConfigManager::getInstance();
  String topic = baseTopic;
  String rootTopic = configManager.getParamValueFromID("mqttTopic");
  rootTopic.replace(PSTR("{id}"), configManager.getChipID());
  topic.replace(PSTR("%st%"), rootTopic);
  topic.replace(PSTR("%cm%"), cmd);

  return topic;
}
#ifdef MQTTAUTODISCOVER

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

  publishHassTopic(PSTR("sensor"),
    PSTR("cpm"),
    PSTR("CPM"),
    PSTR("CPM"),
    PSTR("CPM"),
    PSTR(""),
    PSTR(""),
    PSTR(""),
    PSTR(""),
    PSTR(""),
    { {"unit_of_meas", "CPM"},
      { "ic", "mdi:pulse" }});

  publishHassTopic(PSTR("sensor"),
    PSTR("cpm5"),
    PSTR("CPM5"),
    PSTR("CPM5"),
    PSTR("CPM5"),
    PSTR(""),
    PSTR(""),
    PSTR(""),
    PSTR(""),
    PSTR(""),
    { {"unit_of_meas", "CPM"},
      { "ic", "mdi:pulse" }});

  publishHassTopic(PSTR("sensor"),
    PSTR("cpm15"),
    PSTR("CPM15"),
    PSTR("CPM15"),
    PSTR("CPM15"),
    PSTR(""),
    PSTR(""),
    PSTR(""),
    PSTR(""),
    PSTR(""),
    { {"unit_of_meas", "CPM"},
      { "ic", "mdi:pulse" }});

  publishHassTopic(PSTR("sensor"),
    PSTR("usv"),
    PSTR("\u00B5Sv/h"),
    PSTR("uSv"),
    PSTR("uSv"),
    PSTR(""),
    PSTR(""),
    PSTR(""),
    PSTR(""),
    PSTR(""),
    { {"unit_of_meas", "\u00B5S/h"},
      { "ic", "mdi:radioactive" }});

#ifdef ESPGEIGER_HW
  publishHassTopic(PSTR("sensor"),
    PSTR("hv"),
    PSTR("HV"),
    PSTR("HV"),
    PSTR("HV"),
    PSTR(""),
    PSTR(""),
    PSTR(""),
    PSTR(""),
    PSTR(""),
    { {"unit_of_meas", "V"},
      { "ic", "mdi:lightning-bolt" }});
#endif

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
                               std::vector<std::pair<char*, char*>> additionalEntries
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

  char buffer[MQTT_JSON_BUFFER_SIZE];
  serializeJson(json, buffer);

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
  removeHassTopic(PSTR("sensor"), PSTR("cpm"));
  removeHassTopic(PSTR("sensor"), PSTR("cpm5"));
  removeHassTopic(PSTR("sensor"), PSTR("cpm15"));
  removeHassTopic(PSTR("sensor"), PSTR("usv"));
#ifdef ESPGEIGER_HW
  removeHassTopic(PSTR("sensor"), PSTR("hv"));
#endif
}

void MQTT_Client::removeHassTopic(const String& mqttDeviceType, const String& mqttDeviceName)
{
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
    if (memcmp(payload, "online", sizeof(payload)) == 0) {
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
