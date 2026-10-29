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
#include <EGPortal.h>

#include "Corium/Logger/Logger.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Net/WifiInfo.h"
#include "Corium/Net/WifiSetup.h"
#include "Corium/System/FastMillis.h"
#include "Corium/System/Uptime.h"

ImprovSerial improvSerial;

#ifndef APP_FW_NAME
#define APP_FW_NAME THING_NAME
#endif

#if defined(ESP8266)
  static const char* IMPROV_CHIPFAMILY = "ESP8266";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  static const char* IMPROV_CHIPFAMILY = "ESP32-S3";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  static const char* IMPROV_CHIPFAMILY = "ESP32-C3";
#else
  static const char* IMPROV_CHIPFAMILY = "ESP32";
#endif

static std::string device_url() {
  char ip[16], url[32];
  Wifi::formatIP(ip, sizeof(ip), Wifi::localIP());
  snprintf_P(url, sizeof(url), PSTR("http://%s/"), ip);
  return std::string(url);
}

void ImprovSerial::begin() {
  _position = 0;
  _state = improv::STATE_AUTHORIZED;
  while (Serial.available()) Serial.read();
}

void ImprovSerial::poll() {
  while (Serial.available()) {
    uint8_t b = Serial.read();
    consume_byte(b);
  }
}

bool ImprovSerial::consume_byte(uint8_t b) {
  uint32_t now = fast_millis();
  if (_last_byte_ms && (now - _last_byte_ms) > 2000) {
    _position = 0;
  }
  _last_byte_ms = now;

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
  uint8_t frame[256];   // 11 bytes framing + payload
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
  auto payload = improv::build_rpc_response(cmd, datum, false);
  send_frame(improv::TYPE_RPC_RESPONSE, payload);
}

void ImprovSerial::send_error(improv::Error err) {
  send_frame(improv::TYPE_ERROR_STATE, { (uint8_t)err });
}

// boot still joining the saved wifi; uptime ticks from the loop, the ms clock from setup
static const uint32_t IMPROV_JOIN_MS = 30000;

bool ImprovSerial::on_command(improv::ImprovCommand cmd) {
  // mid join, a frame here would nest another
  if (_in_wifi_settings) {
    send_error(improv::ERROR_UNKNOWN);
    return false;
  }
  switch (cmd.command) {
    case improv::WIFI_SETTINGS:
      handle_wifi_settings(cmd.ssid, cmd.password);
      break;
    case improv::GET_CURRENT_STATE: {
      if (Wifi::connected_raw()) {
        _state = improv::STATE_PROVISIONED;
      } else {
        // can take 50ms on esp32
        static bool s_creds_cached = false;
        static bool s_creds_value = false;
        if (!s_creds_cached) {
          s_creds_value = Wifi::hasSavedCreds();
          s_creds_cached = true;
        }
        bool joining = fast_millis() < IMPROV_JOIN_MS && Uptime::seconds() < (time_t)(IMPROV_JOIN_MS / 1000);
        _state = (joining && s_creds_value) ? improv::STATE_PROVISIONING : improv::STATE_AUTHORIZED;
      }
      set_state(_state);
      if (_state == improv::STATE_PROVISIONED) {
        send_response_data(improv::GET_CURRENT_STATE, { device_url() });
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
  return false;   // resets _position
}

void ImprovSerial::on_error(improv::Error err) {
  // no reply, installer retries
  Log::debug(PSTR("Improv: parser error %u"), (unsigned)err);
}

void ImprovSerial::send_device_info() {
  std::vector<std::string> info = {
    APP_FW_NAME,
    RELEASE_VERSION,
    IMPROV_CHIPFAMILY,
    DeviceInfo::hostname(),
  };
  send_response_data(improv::GET_DEVICE_INFO, info);
}

void ImprovSerial::send_wifi_networks() {
  int n = Wifi::scanNetworks(false, false);
  int8_t best_idx[16];
  uint8_t kept = 0;
  for (int i = 0; i < n && kept < 16; i++) {
    String ssid = Wifi::scanSSID(i);
    if (!ssid.length()) continue;
    bool seen = false;
    for (uint8_t k = 0; k < kept; k++) {
      if (ssid == Wifi::scanSSID(best_idx[k])) {
        if (Wifi::scanRSSI(i) > Wifi::scanRSSI(best_idx[k])) best_idx[k] = (int8_t)i;
        seen = true;
        break;
      }
    }
    if (!seen) best_idx[kept++] = (int8_t)i;
  }
  for (uint8_t k = 0; k < kept; k++) {
    int i = best_idx[k];
    bool open = Wifi::scanOpen(i);
    std::vector<std::string> entry = {
      std::string(Wifi::scanSSID(i).c_str()),
      String(Wifi::scanRSSI(i)).c_str(),
      open ? "NO" : "YES"
    };
    send_response_data(improv::GET_WIFI_NETWORKS, entry);
  }
  send_response_data(improv::GET_WIFI_NETWORKS, {});   // empty = end of list
}

void ImprovSerial::handle_wifi_settings(const std::string& ssid,
                                       const std::string& password) {
  // as the portal, one that can never join isn't saved
  if (!EGPortal::passOk(password.c_str())) {
    send_error(improv::ERROR_UNABLE_TO_CONNECT);
    return;
  }
  _in_wifi_settings = true;
  set_state(improv::STATE_PROVISIONING);
  Wifi::saveBackupCreds();
  Wifi::onImprovCreds(ssid.c_str(), password.c_str());
  // else the wait loop sends the old IP
  Wifi::join(ssid.c_str(), password.c_str());

  // esp-web-tools gives up at 30s
  uint32_t start = fast_millis();
  while (fast_millis() - start < 25000) {
    if (Wifi::connected_raw()) {
      set_state(improv::STATE_PROVISIONED);
      send_response_data(improv::WIFI_SETTINGS, { device_url() });
      Log::console(PSTR("Improv: provisioned ssid='%s' ip=%s"),
                   ssid.c_str(), Wifi::localIP().toString().c_str());
      _in_wifi_settings = false;
      return;
    }
    yield();
    while (Serial.available()) {
      consume_byte((uint8_t)Serial.read());
    }
    delay(50);
  }
  Log::console(PSTR("Improv: WiFi connect timeout for ssid='%s'"), ssid.c_str());
  // installer gone, stop connecting
  Wifi::off();
  set_state(improv::STATE_AUTHORIZED);
  send_error(improv::ERROR_UNABLE_TO_CONNECT);
  _in_wifi_settings = false;
}

