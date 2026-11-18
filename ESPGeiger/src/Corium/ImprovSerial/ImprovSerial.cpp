/*
  ImprovSerial.cpp - Improv WiFi over serial

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
#include "Corium/Util/Words.h"

corium::ImprovSerial improvSerial;

#ifndef APP_FW_NAME
#define APP_FW_NAME THING_NAME
#endif

#if defined(ESP8266)
  static const char* IMPROV_CHIPFAMILY = "ESP8266";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  static const char* IMPROV_CHIPFAMILY = "ESP32-S3";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  static const char* IMPROV_CHIPFAMILY = "ESP32-C3";
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
  static const char* IMPROV_CHIPFAMILY = "ESP32-S2";
#else
  static const char* IMPROV_CHIPFAMILY = "ESP32";
#endif

static void device_url(char* url, size_t cap) {
  char ip[16];
  corium::Wifi::formatIP(ip, sizeof(ip), corium::Wifi::localIP());
  snprintf_P(url, cap, PSTR("http://%s/"), ip);
}

void corium::ImprovSerial::begin() {
  _pos = 0;
  _state = improv::STATE_AUTHORIZED;
  while (Serial.available()) Serial.read();
}

void corium::ImprovSerial::poll() {
  while (Serial.available()) {
    uint8_t b = Serial.read();
    consumeByte(b);
  }
  watch();
}

static constexpr uint32_t FRAME_GAP_MS = 2000;

void corium::ImprovSerial::watch() {
  // Drop a stale part frame here too, so a long quiet spell can't wrap its age
  if (_pos && corium::fastMillis() - _last_byte_ms > FRAME_GAP_MS) _pos = 0;
  if (_joining) finish();
}

bool corium::ImprovSerial::consumeByte(uint8_t b) {
  uint32_t now = corium::fastMillis();
  // Gap in the middle of a frame, start again
  if (_pos && now - _last_byte_ms > FRAME_GAP_MS) {
    _pos = 0;
  }
  _last_byte_ms = now;

  if (_pos >= sizeof(_buf)) _pos = 0;
  _buf[_pos] = b;
  bool consumed = improv::parse_improv_serial_byte(
    _pos, b, _buf,
    [this](improv::ImprovCommand cmd) { return on_command(cmd); },
    [this](improv::Error err) { on_error(err); });
  if (consumed) {
    _pos++;
    return true;
  }
  _pos = 0;
  return false;
}

void corium::ImprovSerial::set_state(improv::State s) {
  _state = s;
  uint8_t b = (uint8_t)s;
  send_frame(improv::TYPE_CURRENT_STATE, &b, 1);
}

static const size_t FRAME_MAX = 256;
static const size_t FRAME_OVERHEAD = 11;   // IMPROV, ver, type, len, sum, \n

void corium::ImprovSerial::send_frame(improv::ImprovSerialType type, const uint8_t* payload, size_t len) {
  uint8_t frame[FRAME_MAX];
  size_t pos = 0;
  if (len > sizeof(frame) - FRAME_OVERHEAD) return;
  frame[pos++] = 'I'; frame[pos++] = 'M'; frame[pos++] = 'P';
  frame[pos++] = 'R'; frame[pos++] = 'O'; frame[pos++] = 'V';
  frame[pos++] = improv::IMPROV_SERIAL_VERSION;
  frame[pos++] = (uint8_t)type;
  frame[pos++] = (uint8_t)len;
  memcpy(frame + pos, payload, len);
  pos += len;
  uint8_t checksum = 0;   // sum of every byte, header included
  for (size_t i = 0; i < pos; i++) checksum += frame[i];
  frame[pos++] = checksum;
  frame[pos++] = '\n';
  Serial.write(frame, pos);
}

// Same layout as improv's build_rpc_response, including its unused checksum byte
void corium::ImprovSerial::send_response_data(improv::Command cmd, const char* const* datum, uint8_t n) {
  uint8_t out[FRAME_MAX - FRAME_OVERHEAD];
  size_t pos = 2;   // cmd and total len, then len + string for each entry
  for (uint8_t i = 0; i < n; i++) {
    size_t len = strlen(datum[i]);
    if (len + 2 > sizeof(out) - pos) return;   // too big to send
    out[pos++] = (uint8_t)len;
    memcpy(out + pos, datum[i], len);
    pos += len;
  }
  out[0] = cmd;
  out[1] = (uint8_t)(pos - 2);
  out[pos++] = 0;
  send_frame(improv::TYPE_RPC_RESPONSE, out, pos);
}

void corium::ImprovSerial::send_url(improv::Command cmd) {
  char url[32];
  device_url(url, sizeof(url));
  const char* d[] = {url};
  send_response_data(cmd, d, 1);
}

void corium::ImprovSerial::send_error(improv::Error err) {
  uint8_t b = (uint8_t)err;
  send_frame(improv::TYPE_ERROR_STATE, &b, 1);
}

// How long the boot may still be joining the saved wifi. Uptime only ticks from the loop
// and the ms clock runs from setup, so both are checked
static const uint32_t IMPROV_JOIN_MS = 30000;

static improv::State current_state() {
  if (corium::Wifi::connectedRaw()) return improv::STATE_PROVISIONED;
  bool joining = corium::fastMillis() - CORIUM_MS_START < IMPROV_JOIN_MS && corium::Uptime::seconds() < (time_t)(IMPROV_JOIN_MS / 1000);
  // hasSavedCreds() switches to STA, which would drop the hotspot or the portal
  if (!joining || corium::Wifi::apUp()) return improv::STATE_AUTHORIZED;
  static bool s_asked = false;
  static bool s_have = false;
  if (!s_asked) {
    s_have = corium::Wifi::hasSavedCreds();
    s_asked = true;
  }
  return s_have ? improv::STATE_PROVISIONING : improv::STATE_AUTHORIZED;
}

bool corium::ImprovSerial::on_command(improv::ImprovCommand cmd) {
  // Already joining, a new command here would nest another one
  if (_joining) {
    send_error(improv::ERROR_UNKNOWN);
    return false;
  }
  switch (cmd.command) {
    case improv::WIFI_SETTINGS:
      handle_wifi_settings(cmd.ssid, cmd.password);
      break;
    case improv::GET_CURRENT_STATE:
      set_state(current_state());
      if (_state == improv::STATE_PROVISIONED) send_url(improv::GET_CURRENT_STATE);
      break;
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
  return false;   // Resets _pos
}

void corium::ImprovSerial::on_error(improv::Error err) {
  // No reply, the installer retries
  corium::Log::debug(PSTR("Improv: parser error %u"), (unsigned)err);
}

void corium::ImprovSerial::send_device_info() {
  const char* about[] = {
    APP_FW_NAME,  // sent from ram
    RELEASE_VERSION,
    IMPROV_CHIPFAMILY,
    corium::DeviceInfo::hostname(),
  };
  send_response_data(improv::GET_DEVICE_INFO, about, 4);
}

void corium::ImprovSerial::send_wifi_networks() {
  // One row per ssid using its strongest AP, strongest first, no rssi floor
  EGPortal::ScanEntry aps[16];
  size_t kept = EGPortal::processScan(aps, 16, corium::Wifi::scanNetworks(false, false), INT8_MIN);
  for (size_t k = 0; k < kept; k++) {
    char rssi[12];
    snprintf_P(rssi, sizeof(rssi), W_FMT_D, (int)aps[k].rssi);
    const char* entry[] = {
      aps[k].ssid,
      rssi,
      corium::Wifi::encOpen(aps[k].enc) ? "NO" : "YES"
    };
    send_response_data(improv::GET_WIFI_NETWORKS, entry, 3);
  }
  send_response_data(improv::GET_WIFI_NETWORKS, nullptr, 0);   // empty = end of list
  corium::Wifi::scanDelete();
}

static const uint32_t IMPROV_TEST_MS = 25000;   // esp-web-tools gives up at 30s
static const uint32_t IMPROV_MOVED_MS = 2000;   // time to get the url out first

void corium::ImprovSerial::handle_wifi_settings(const std::string& ssid,
                                       const std::string& password) {
  // Same as the portal, creds that can never join aren't saved
  if (!EGPortal::ssidOk(ssid.c_str()) || !EGPortal::passOk(password.c_str())) {
    send_error(improv::ERROR_UNABLE_TO_CONNECT);
    return;
  }
  // fails if the web's test is running or there's no memory
  if (corium::Wifi::holdCreds(ssid.c_str(), password.c_str()) != corium::Wifi::HOLD_OK
      || !corium::Wifi::testJoin(IMPROV_TEST_MS)) {
    send_error(improv::ERROR_UNKNOWN);
    return;
  }
  _joining = true;
  set_state(improv::STATE_PROVISIONING);
}

// The test is stepped by the portal or the wifi module, this only handles its end
void corium::ImprovSerial::finish() {
  if (corium::Wifi::testing()) return;
  _joining = false;
  const char* ssid = corium::Wifi::testSsid();
  bool joined = corium::Wifi::testState() == EGPortal::JOIN_OK;
  if (joined && corium::Wifi::keepJoined(IMPROV_MOVED_MS)) {
    set_state(improv::STATE_PROVISIONED);
    send_url(improv::WIFI_SETTINGS);
    char ip[16];
    corium::Wifi::formatIP(ip, sizeof(ip), corium::Wifi::localIP());
    corium::Log::console(PSTR("Improv: Provisioned ssid='%s' ip=%s"), ssid, ip);
    return;
  }
  corium::Log::console(joined ? PSTR("Improv: WiFi joined for ssid='%s', not kept")
                              : PSTR("Improv: WiFi not joined for ssid='%s'"), ssid);
  set_state(improv::STATE_AUTHORIZED);
  // Joined but not kept, offline this boot so there's no url to give
  send_error(joined ? improv::ERROR_UNKNOWN : improv::ERROR_UNABLE_TO_CONNECT);
}

