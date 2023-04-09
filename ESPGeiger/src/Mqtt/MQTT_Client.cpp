/*
  MQTTClient.cpp - MQTT connection class
  
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
  if (connect(clientId, configManager.getParamValueFromID("mqttUser"), configManager.getParamValueFromID("mqttPassword"), buildTopic(teleTopic, topicLWT).c_str(), 2, false, lwtOffline ))
  {
    yield();
    Log::console(PSTR("MQTT: Connected!"));
    status.mqtt_connected = true;
    publish(buildTopic(teleTopic, topicLWT).c_str(), lwtOnline, false);
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
  topic.replace("%st%", clientId);
  topic.replace("%cm%", cmnd);

  return topic;
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

  mqttEnabled = true;
  const char* hostName = configManager.getHostName();
  strcat(clientId, hostName);

}