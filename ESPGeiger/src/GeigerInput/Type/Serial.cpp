/*
  GeigerInput/Type/Serial.cpp - Class for Serial type counter

  Copyright (C) 2024 @steadramon

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
#include "Serial.h"

#if GEIGER_IS_SERIAL(GEIGER_TYPE)

#include "../../Logger/Logger.h"
#include "../../Prefs/EGPrefs.h"
#include "../../Util/DeviceInfo.h"
#include "../SerialFormat.h"

static EspSoftwareSerial::UART geigerPort;

GeigerSerial::GeigerSerial() {
};

void GeigerSerial::begin() {
  uint8_t st = (uint8_t)EGPrefs::getUInt("input", "serial_type");
  if (SerialFormat::is_known(st)) _serial_type = st;
  uint32_t    baud = SerialFormat::baud_for(_serial_type);
  const char* name = SerialFormat::name_for(_serial_type);
  if (baud == 0) baud = 9600;    // safety fallback for an unknown saved pref
  _use_cps = false;
  if (name && EGPrefs::getString("input", "geiger_model")[0] == '\0') {
    DeviceInfo::setGeigermodel(name);
  }
  Log::console(PSTR("GeigerSerial: %s (type %d) BAUD: %lu RXPIN: %d hasCPS=%d"),
               name ? name : "?", _serial_type, baud, _rx_pin,
               SerialFormat::has_cps(_serial_type) ? 1 : 0);
  if (_rx_pin == 1 || _rx_pin == 3 || _tx_pin == 1 || _tx_pin == 3) {
    Log::console(PSTR("GeigerSerial: ERROR rx/tx pin clashes with UART0"));
  }
  // 64-byte RX buffer at 115200 (GC10NX) gives ISR margin under WiFi PHY work.
  geigerPort.begin(baud, SWSERIAL_8N1, _rx_pin, _tx_pin, false, 64);
  uint32_t skip = 6000000UL / baud;
  if (skip < 5)   skip = 5;
  if (skip > 500) skip = 500;
  _poll_skip = (uint16_t)skip;
}

void GeigerSerial::pullSerial() {
  uint8_t maxRead = sizeof(_serial_buffer);
  while (geigerPort.available() && maxRead--) {
    char input = geigerPort.read();
    // Non-printable byte: discard the rest of this line, don't bother the parser.
    if (input != '\n' && input != '\r' && !isPrintable((uint8_t)input)) {
      _serial_idx = 0;
      _serial_buffer[0] = '\0';
      while (maxRead && geigerPort.available()) {
        maxRead--;
        if (geigerPort.read() == '\n') break;
      }
      continue;
    }
    _serial_buffer[_serial_idx++] = input;
    if (input == '\n') {
      _serial_buffer[_serial_idx++] = '\0';
      handleSerial(_serial_buffer);
      _serial_idx = 0;
      _serial_buffer[0] = '\0';
    }
    if (_serial_idx >= sizeof(_serial_buffer) - 2) {
      _serial_idx = 0;
      _serial_buffer[0] = '\0';
    }
  }
}

void GeigerSerial::loop() {
  if (_loop_c < _poll_skip && _serial_idx == 0) {
    _loop_c++;
    return;
  }
  _loop_c = 0;
  pullSerial();
  if (serial_value <= 0) return;
  if (millis() - last_serial > 10000) serial_value = 0;
}

void GeigerSerial::secondTicker() {
  // _use_cps is a per-second flag (set in handleSerial when wire CPS
  // arrived, reset here) so a producer can toggle `show cps` mid-run.
  if (_use_cps) {
    int c = (int)partial_clicks;
    partial_clicks = 0;
    setCounter(c, false);
    _use_cps = false;
    return;
  }
  // CPM path: synthesise fractional clicks. INV_60 avoids the soft-float divide.
  static constexpr float INV_60 = 1.0f / 60.0f;
  partial_clicks += (float)serial_value * INV_60;
  if (partial_clicks >= 1.0f) {
    int full_clicks = (int)partial_clicks;
    partial_clicks -= full_clicks;
    setCounter(full_clicks, false);
  }
}

// Reset SoftSerial if we see this many consecutive un-parseable lines.
#define GEIGERSERIAL_BAD_LIMIT 20

void GeigerSerial::drainPort() {
  unsigned long now = millis();
  // Throttle console log to once a minute.
  if (now - _last_drain_log > 60000UL) {
    Log::console(PSTR("GeigerSerial: %u bad lines, draining port"), _bad_streak);
    _last_drain_log = now;
  } else {
    Log::debug(PSTR("GeigerSerial: %u bad lines, draining port"), _bad_streak);
  }
  int n = geigerPort.available();
  while (n-- > 0) geigerPort.read();
  _serial_idx = 0;
  _serial_buffer[0] = '\0';
  _bad_streak = 0;
  _last_drain = now;
}

void GeigerSerial::handleSerial(char* input) {
  int _scpm = 0;
  int _scps = -1;
  if (!SerialFormat::parse_cpm(_serial_type, input, &_scpm, &_scps)) {
    _bad_streak++;
    if (_bad_streak >= GEIGERSERIAL_BAD_LIMIT) drainPort();
    return;
  }

  Log::debug(PSTR("GeigerSerial: Loop - %d"), _scpm);
  setLastBlip();
  serial_value = _scpm;
  if (_scps >= 0) {
    partial_clicks += (float)_scps;
    _use_cps = true;
  }
  last_serial = millis();
  _bad_streak = max((int)_bad_streak - 3, 0);
}
#endif // GEIGER_IS_SERIAL
