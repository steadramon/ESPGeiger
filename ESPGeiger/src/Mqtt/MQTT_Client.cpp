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

#include "MQTT_Client.h"
#include "ArduinoJson.h"
#include "../Logger/Logger.h"

MQTT_Client::MQTT_Client()
    : PubSubClient(espClient)
{
}
void MQTT_Client::disconnect()
{
  PubSubClient::disconnect();
  mqttEnabled = true;
}
void MQTT_Client::loop()
{
  if (!mqttEnabled) {
    return;
  }

  if (!connected())
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
  else
  {
    connectionAtempts = 0;
    status.mqtt_connected = true;
  }

  if (connectionAtempts > connectionTimeout)
  {
    Log::console(PSTR("Unable to connect to MQTT Server after many atempts. Restarting..."));
    ESP.restart();
  }

  PubSubClient::loop();

  unsigned long now = millis();
  if (now - lastPing > pingInterval && connected())
  {
    status.led.Blink(500, 500);
    ConfigManager &configManager = ConfigManager::getInstance();
    lastPing = now;
    const size_t capacity = JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(17) + 22 + 20 + 20;
    DynamicJsonDocument doc(capacity);
    doc["uptime"] = configManager.getUptimeString();
    doc["board"] = configManager.GetChipModel();
    doc["model"] = configManager.getParamValueFromID("geigerModel");
    doc["free_mem"] = ESP.getFreeHeap();
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    char buffer[1048];
    serializeJson(doc, buffer);
    Log::console(PSTR("MQTT: %s"), buffer);

    publish(buildTopic(teleTopic, topicStatus).c_str(), buffer, false);

    float avgcpm = gcounter.get_cpm();

    publish(buildTopic(statTopic, "CPM").c_str(), String(avgcpm).c_str(), false);

    float usv =  gcounter.get_usv();
    publish(buildTopic(statTopic, "uSv").c_str(), String(usv).c_str(), false);

    avgcpm = gcounter.get_cpm5();
    publish(buildTopic(statTopic, "CPM5").c_str(), String(avgcpm).c_str(), false);

    avgcpm = gcounter.get_cpm15();
    publish(buildTopic(statTopic, "CPM15").c_str(), String(avgcpm).c_str(), false);

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
    publishHassTopic("sensor",
      "cpm",
      "CPM",
      "CPM",
      "CPM",
      "",
      "",
      "",
      "",
      "",
      { {"unit_of_meas", "CPM"},
        { "ic", "mdi:pulse" }});
    publishHassTopic("sensor",
      "cpm5",
      "CPM5",
      "CPM5",
      "CPM5",
      "",
      "",
      "",
      "",
      "",
      { {"unit_of_meas", "CPM"},
        { "ic", "mdi:pulse" }});

    publishHassTopic("sensor",
      "cpm15",
      "CPM15",
      "CPM15",
      "CPM15",
      "",
      "",
      "",
      "",
      "",
      { {"unit_of_meas", "CPM"},
        { "ic", "mdi:pulse" }});

    publishHassTopic("sensor",
      "usv",
      "\u00B5Sv/h",
      "uSv",
      "uSv",
      "",
      "",
      "",
      "",
      "",
      { {"unit_of_meas", "\u00B5S/h"},
        { "ic", "mdi:radioactive" }});
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
  topic.replace("%st%", configManager.getHostName());
  topic.replace("%cm%", cmnd);

  return topic;
}

void MQTT_Client::publishHassTopic(const String& mqttDeviceType,
                               const String& mattDeviceName,
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
  json["stat_t"] = String("~/stat/") + stateTopic;
  json["uniq_id"] = configManager.getHostName() + String("_") + mattDeviceName;

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
  path.concat("/");
  path.concat(mqttDeviceType);
  path.concat("/");
  path.concat(configManager.getHostName());
  path.concat("-");
  path.concat(mattDeviceName);
  path.concat("/config");

  publish(path.c_str(), buffer);
}

void MQTT_Client::removeHASSConfig()
{
  removeHassTopic("sensor", "cpm");
  removeHassTopic("sensor", "cpm5");
  removeHassTopic("sensor", "cpm15");
  removeHassTopic("sensor", "usv");
}

void MQTT_Client::removeHassTopic(const String& mqttDeviceType, const String& mattDeviceName)
{
  ConfigManager &configManager = ConfigManager::getInstance();

  const char* _discovery_topic = configManager.getParamValueFromID("hassDisc");
  if (_discovery_topic == NULL) {
    _discovery_topic = MQTT_DISCOVERY_TOPIC;
  }

  String path = _discovery_topic;
  path.concat("/");
  path.concat(mqttDeviceType);
  path.concat("/");
  path.concat(configManager.getHostName());
  path.concat("/");
  path.concat(mattDeviceName);
  path.concat("/config");

  publish(path.c_str(), "", true);
}

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

  mqttEnabled = true;

}