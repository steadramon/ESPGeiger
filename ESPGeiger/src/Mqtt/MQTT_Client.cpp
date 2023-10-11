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

MQTT_Client::MQTT_Client()
    : PubSubClient(espClient)
{
#ifdef ESP8266
  // setTimeout in msecs
  espClient.setTimeout(200 * 100);
#else
  // setTimeout in secs
  uint32_t timeout = (20);
  espClient.setTimeout(timeout);
#endif
}
void MQTT_Client::disconnect()
{
  PubSubClient::disconnect();
  espClient.stop();
  mqttEnabled = true;
}
void MQTT_Client::loop()
{

  if (!mqttEnabled) {
    return;
  }
  unsigned long now = millis();
  if (now - status.last_mqtt < 100) {
    return;
  }

  status.last_mqtt = now;

  if (PubSubClient::loop())
  {
    connectionAtempts = 0;
    status.mqtt_connected = true;
  }
  else
  {
    status.mqtt_connected = false;
    if (millis() - lastConnectionAtempt > reconnectionInterval)
    {
      lastConnectionAtempt = millis();
      connectionAtempts++;

      lastPing = millis();
      reconnect();
    }
  }

  if (connectionAtempts > connectionTimeout)
  {
    Log::console(PSTR("Unable to connect to MQTT Server after many atempts. Restarting..."));
    ESP.restart();
  }

  if (now - lastPing > pingInterval && connected())
  {
    status.led.Blink(500, 500);
    ConfigManager &configManager = ConfigManager::getInstance();
    lastPing = now - (now % 1000);
    const size_t capacity = JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(17) + 22 + 20 + 20;
    DynamicJsonDocument doc(capacity);
    char buffer[1048];

    doc["uptime"] = configManager.getUptimeString();
    doc["board"] = configManager.GetChipModel();
    doc["model"] = configManager.getParamValueFromID("geigerModel");
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["c_total"] = status.total_clicks;
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

    publish(buildTopic(teleTopic, topicStatus).c_str(), buffer, false);

    publish(buildTopic(statTopic, PSTR("CPM")).c_str(), String(status.geigerTicks.get()*60.0).c_str(), false);

    publish(buildTopic(statTopic, PSTR("uSv")).c_str(), String(gcounter.get_usv()).c_str(), false);

#ifndef DISABLE_MQTT_CPS
    publish(buildTopic(statTopic, PSTR("CPS")).c_str(), String(status.geigerTicks.get()).c_str(), false);
#endif

    publish(buildTopic(statTopic, PSTR("CPM5")).c_str(), String(status.geigerTicks5.get()*60.0).c_str(), false);

    publish(buildTopic(statTopic, PSTR("CPM15")).c_str(), String(status.geigerTicks15.get()*60.0).c_str(), false);

    status.last_send = millis();
  }

}

void MQTT_Client::reconnect()
{
  ConfigManager &configManager = ConfigManager::getInstance();

  if (configManager.getParamValueFromID("mqttServer") == NULL)
  {
    mqttEnabled = false;
    return;
  }
  setServer(configManager.getParamValueFromID("mqttServer"), atoi(configManager.getParamValueFromID("mqttPort")));

  Log::console(PSTR("MQTT: Attempting connection ... %s:%s"), configManager.getParamValueFromID("mqttServer"), configManager.getParamValueFromID("mqttPort"));
  if (connect(configManager.getHostName(), configManager.getParamValueFromID("mqttUser"), configManager.getParamValueFromID("mqttPassword"), buildTopic(teleTopic, topicLWT).c_str(), 2, false, lwtOffline ))
  {
    yield();
    Log::console(PSTR("MQTT: Connected!"));
    status.mqtt_connected = true;
    publish(buildTopic(teleTopic, topicLWT).c_str(), lwtOnline, false);
#ifdef MQTTAUTODISCOVER
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
#endif
  }
  else
  {
    status.mqtt_connected = false;
    Log::console(PSTR("MQTT: failed, rc=%i"), state());
  }

}

String MQTT_Client::buildTopic(const char *baseTopic, const char *cmnd)
{
  ConfigManager &configManager = ConfigManager::getInstance();
  String topic = baseTopic;
  topic.replace(PSTR("%st%"), configManager.getHostName());
  topic.replace(PSTR("%cm%"), cmnd);

  return topic;
}
#ifdef MQTTAUTODISCOVER
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

  publish(path.c_str(), buffer);
}

void MQTT_Client::removeHASSConfig()
{
  removeHassTopic(PSTR("sensor"), PSTR("cpm"));
  removeHassTopic(PSTR("sensor"), PSTR("cpm5"));
  removeHassTopic(PSTR("sensor"), PSTR("cpm15"));
  removeHassTopic(PSTR("sensor"), PSTR("usv"));
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

  publish(path.c_str(), "", true);
  yield();
}
#endif
void MQTT_Client::begin()
{
  ConfigManager &configManager = ConfigManager::getInstance();
  const char* _mqtt_server = configManager.getParamValueFromID("mqttServer");
  const char* _mqtt_user = configManager.getParamValueFromID("mqttUser");
  const char* _mqtt_pass = configManager.getParamValueFromID("mqttPassword");

  if ((_mqtt_server == NULL) || (_mqtt_user == NULL) || (_mqtt_pass == NULL)) {
    mqttEnabled = false;
    return;
  }
  setServer(configManager.getParamValueFromID("mqttServer"), atoi(configManager.getParamValueFromID("mqttPort")));
  setBufferSize(MQTT_MAX_PACKET_SIZE);
  setKeepAlive(30);
  setSocketTimeout(4);
  mqttEnabled = true;
}
#endif