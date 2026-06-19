/*
  ImprovSerial.cpp - see header.

  Copyright (C) 2026 @steadramon

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
#include "ImprovSerial.h"
#include <cstring>

#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include "../Logger/Logger.h"
#include "../Util/DeviceInfo.h"
#include "../Util/Wifi.h"
#include "../Util/FastMillis.h"

ImprovSerial improvSerial;

#if defined(ESP8266)
  static const char* IMPROV_CHIPFAMILY = "ESP8266";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  static const char* IMPROV_CHIPFAMILY = "ESP32-S3";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  static const char* IMPROV_CHIPFAMILY = "ESP32-C3";
#else
  static const char* IMPROV_CHIPFAMILY = "ESP32";
#endif

void ImprovSerial::begin() {
  _position = 0;
  _state = improv::STATE_AUTHORIZED;
}

void ImprovSerial::poll() {
  while (Serial.available()) {
    uint8_t b = Serial.read();
    consume_byte(b);
  }
}

bool ImprovSerial::consume_byte(uint8_t b) {
  if (_position >= sizeof(_buffer)) _position = 0;
  _buffer[_position] = b;
  bool consumed = improv::parse_improv_serial_byte(
    _position, b, _buffer,
    [this](improv::ImprovCommand cmd) { return on_command(cmd); },
    [this](improv::Error err) { on_error(err); });
  if (consumed) {
    _position++;
    return true;
  }
  _position = 0;
  return false;
}

void ImprovSerial::set_state(improv::State s) {
  _state = s;
  send_frame(improv::TYPE_CURRENT_STATE, { (uint8_t)s });
}

void ImprovSerial::send_frame(improv::ImprovSerialType type,
                              const std::vector<uint8_t>& payload) {
  // Stack buffer sized for the largest frame we send (device info /
  // WiFi network list / URL). 11 byte overhead + payload, single write.
  uint8_t frame[256];
  size_t pos = 0;
  if (payload.size() > sizeof(frame) - 11) return;
  frame[pos++] = 'I'; frame[pos++] = 'M'; frame[pos++] = 'P';
  frame[pos++] = 'R'; frame[pos++] = 'O'; frame[pos++] = 'V';
  frame[pos++] = improv::IMPROV_SERIAL_VERSION;
  frame[pos++] = (uint8_t)type;
  frame[pos++] = (uint8_t)payload.size();
  for (uint8_t b : payload) frame[pos++] = b;
  uint8_t checksum = 0;
  for (size_t i = 0; i < pos; i++) checksum += frame[i];
  frame[pos++] = checksum;
  frame[pos++] = '\n';
  Serial.write(frame, pos);
}

void ImprovSerial::send_response_data(improv::Command cmd,
                                     const std::vector<std::string>& datum) {
  auto data = improv::build_rpc_response(cmd, datum, false);
  send_frame(improv::TYPE_RPC_RESPONSE, data);
}

void ImprovSerial::send_error(improv::Error err) {
  send_frame(improv::TYPE_ERROR_STATE, { (uint8_t)err });
}

bool ImprovSerial::on_command(improv::ImprovCommand cmd) {
  switch (cmd.command) {
    case improv::WIFI_SETTINGS:
      handle_wifi_settings(cmd.ssid, cmd.password);
      break;
    case improv::GET_CURRENT_STATE: {
      // Reflect actual WiFi state so a saved-creds boot reports PROVISIONED.
      if (WiFi.status() == WL_CONNECTED &&
          _state != improv::STATE_PROVISIONED) {
        _state = improv::STATE_PROVISIONED;
      }
      set_state(_state);
      // Per the Improv spec, an already-provisioned device follows the
      // state byte with the post-provisioning RPC result carrying the URL.
      // RPC result command byte echoes the command we're answering.
      if (_state == improv::STATE_PROVISIONED) {
        char url[48];
        snprintf(url, sizeof(url), "http://%s/",
                 WiFi.localIP().toString().c_str());
        send_response_data(improv::GET_CURRENT_STATE, { std::string(url) });
      }
      break;
    }
    case improv::GET_DEVICE_INFO:
      send_device_info();
      break;
    case improv::GET_WIFI_NETWORKS:
      send_wifi_networks();
      break;
    default:
      send_error(improv::ERROR_UNKNOWN_RPC);
      break;
  }
  // Return false so consume_byte resets _position after a complete frame.
  return false;
}

void ImprovSerial::on_error(improv::Error err) {
  send_error(err);
}

void ImprovSerial::send_device_info() {
  std::string fw;
#ifdef GEIGER_MODEL
  const char* model = GEIGER_MODEL;
  if (strncmp(model, "ESPGeiger-", 10) == 0) {
    fw = model;
  } else {
    fw = std::string("ESPGeiger ") + model;
  }
#else
  fw = "ESPGeiger";
#endif
  std::vector<std::string> info = {
    fw,
    RELEASE_VERSION,
    IMPROV_CHIPFAMILY,
    DeviceInfo::hostname(),
  };
  send_response_data(improv::GET_DEVICE_INFO, info);
}

void ImprovSerial::send_wifi_networks() {
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    bool open = (WiFi.encryptionType(i) ==
#ifdef ESP8266
                 ENC_TYPE_NONE
#else
                 WIFI_AUTH_OPEN
#endif
                );
    std::vector<std::string> entry = {
      std::string(WiFi.SSID(i).c_str()),
      String(WiFi.RSSI(i)).c_str(),
      open ? "NO" : "YES"
    };
    send_response_data(improv::GET_WIFI_NETWORKS, entry);
  }
  // Empty payload terminates the list per protocol.
  send_response_data(improv::GET_WIFI_NETWORKS, {});
}

void ImprovSerial::handle_wifi_settings(const std::string& ssid,
                                       const std::string& password) {
  set_state(improv::STATE_PROVISIONING);
  Wifi::saveBackupCreds();
  // Signals the captive-portal busy-wait (if active) to exit with these
  // creds. No-op when called outside the portal phase.
  Wifi::onImprovCreds(ssid.c_str(), password.c_str());
  WiFi.persistent(true);
  WiFi.begin(ssid.c_str(), password.c_str());

  uint32_t start = fast_millis();
  while (fast_millis() - start < 30000) {
    if (WiFi.status() == WL_CONNECTED) {
      set_state(improv::STATE_PROVISIONED);
      char url[48];
      snprintf(url, sizeof(url), "http://%s/",
               WiFi.localIP().toString().c_str());
      send_response_data(improv::WIFI_SETTINGS, { std::string(url) });
      Log::console(PSTR("Improv: provisioned ssid='%s' ip=%s"),
                   ssid.c_str(), WiFi.localIP().toString().c_str());
      return;
    }
    // Service watchdog and keep parsing Improv bytes during the wait.
    yield();
    while (Serial.available()) {
      consume_byte((uint8_t)Serial.read());
    }
    delay(50);
  }
  Log::console(PSTR("Improv: WiFi connect timeout for ssid='%s'"), ssid.c_str());
  set_state(improv::STATE_AUTHORIZED);
  send_error(improv::ERROR_UNABLE_TO_CONNECT);
}

