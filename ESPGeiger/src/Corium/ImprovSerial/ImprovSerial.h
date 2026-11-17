/*
  ImprovSerial.h - Improv WiFi over serial. Lets esp-web-tools provision
  WiFi credentials right after install, no AP captive portal step.

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
#ifndef CORIUM_IMPROVSERIAL_H
#define CORIUM_IMPROVSERIAL_H

#include <Arduino.h>
#include <improv.h>
#include <string>

namespace corium {
class ImprovSerial {
public:
  void begin();
  // true = part of an Improv frame, caller drops it; false = caller keeps it
  bool consumeByte(uint8_t b);
  // setup and the portal wait only, else it races the console on Serial.read()
  void poll();
  // a WIFI_SETTINGS test ended: its answer; each console pass
  void watch();

private:
  void set_state(improv::State s);
  void send_frame(improv::ImprovSerialType type, const uint8_t* payload, size_t len);
  void send_response_data(improv::Command cmd, const char* const* datum, uint8_t n);
  void send_url(improv::Command cmd);
  void send_error(improv::Error err);
  bool on_command(improv::ImprovCommand cmd);
  void on_error(improv::Error err);
  void send_device_info();
  void send_wifi_networks();
  void handle_wifi_settings(const std::string& ssid, const std::string& password);
  void finish();

  uint8_t       _buf[128];   // one frame in
  size_t        _pos = 0;
  improv::State _state = improv::STATE_AUTHORIZED;
  uint32_t      _last_byte_ms = 0;
  bool          _joining = false;
};
}  // namespace corium

extern corium::ImprovSerial improvSerial;

#endif
