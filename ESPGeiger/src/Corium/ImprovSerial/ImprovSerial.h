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
#ifndef IMPROVSERIAL_H
#define IMPROVSERIAL_H

#include <Arduino.h>
#include <improv.h>
#include <string>
#include <vector>

class ImprovSerial {
public:
  void begin();
  // Feed one byte to the parser. Returns true if it was part of an
  // in-flight Improv frame; the caller should drop it. Returns false
  // for out-of-frame bytes, which the caller keeps.
  bool consume_byte(uint8_t b);
  // Pull pending Serial bytes through the parser. Only call from contexts
  // where SerialCommand's main-loop polling isn't running (setup, the
  // captive-portal busy-wait); otherwise the two race on Serial.read().
  void poll();
  bool isProvisioning() const { return _state == improv::STATE_PROVISIONING; }

private:
  void set_state(improv::State s);
  void send_frame(improv::ImprovSerialType type, const std::vector<uint8_t>& payload);
  void send_response_data(improv::Command cmd, const std::vector<std::string>& datum);
  void send_error(improv::Error err);
  bool on_command(improv::ImprovCommand cmd);
  void on_error(improv::Error err);
  void send_device_info();
  void send_wifi_networks();
  void handle_wifi_settings(const std::string& ssid, const std::string& password);

  uint8_t       _buffer[128];
  size_t        _position = 0;
  improv::State _state = improv::STATE_AUTHORIZED;
  uint32_t      _last_byte_ms = 0;
  bool          _in_wifi_settings = false;
};

extern ImprovSerial improvSerial;

#endif
