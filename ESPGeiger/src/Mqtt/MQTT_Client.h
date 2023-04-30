/*
  MQTT_Client.h - MQTT connection class
  
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

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#define LOG_TAG "MQTT"

#include "../ConfigManager/ConfigManager.h"
#include "../Status.h"
#include "../Counter/Counter.h"
#include <PubSubClient.h>
#include <WiFiClient.h>

#define MQTT_MAX_PACKET_SIZE 1024
#define MQTT_JSON_BUFFER_SIZE 1024

extern Status status;
extern Counter gcounter;

class MQTT_Client : public PubSubClient {
public:
  static MQTT_Client& getInstance()
  {
    static MQTT_Client instance; 
    return instance;
  }
  void begin();
  void loop();
  void sendStatus();
  void disconnect();
  void removeHASSConfig();
protected:
  WiFiClient espClient;
  void reconnect();

private:
  MQTT_Client();
  static ConfigManager _configManager;
  String buildTopic(const char * baseTopic, const char * cmnd);
  void publishHassTopic(const String& mqttDeviceType,
                        const String& mattDeviceName,
                        const String& displayName,
                        const String& name,
                        const String& stateTopic,
                        const String& deviceType,
                        const String& deviceClass,
                        const String& stateClass = "",
                        const String& entityCat = "",
                        const String& commandTopic = "",
                        std::vector<std::pair<char*, char*>> additionalEntries = {}
                      );
  void removeHassTopic(const String& mqttDeviceType, const String& mattDeviceName);
  unsigned long lastPing = 0;
  unsigned long lastConnectionAtempt = 0;
  uint8_t connectionAtempts = 0;
  bool mqttEnabled = true;

  const unsigned long pingInterval = 1 * 60 * 1000;
  const unsigned long reconnectionInterval = 5 * 1000;
  uint16_t connectionTimeout = 5 * 60 * 1000 / reconnectionInterval;

  const char* teleTopic = "%st%/tele/%cm%";
  const char* statTopic = "%st%/stat/%cm%";

  // tele
  const char* topicStatus = "status";
  const char* topicLWT = "lwt";

  const char* lwtOnline = "Online";
  const char* lwtOffline = "Offline";

  // command
  const char* commandReset = "reset";
  const char* commandStatus = "status";
  const char* commandLog = "log";

};

#endif