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
#include "../../Logger/Logger.h"
#include "../../Prefs/EGPrefs.h"

static EspSoftwareSerial::UART geigerPort;

struct SerialTypeInfo {
  uint8_t     id;
  uint32_t    baud;
  const char* name;
};

// Add new serial types here. Parser cases go in handleSerial().
static const SerialTypeInfo SERIAL_TYPES[] = {
  { GEIGER_STYPE_GC10,      9600,   "GC10" },
  { GEIGER_STYPE_GC10NX,    115200, "GC10Next" },
  { GEIGER_STYPE_MIGHTYOHM, 9600,   "MightyOhm" },
  { GEIGER_STYPE_ESPGEIGER,  115200, "ESPGeiger" },
};
static constexpr uint8_t SERIAL_TYPE_COUNT = sizeof(SERIAL_TYPES) / sizeof(SERIAL_TYPES[0]);

static const SerialTypeInfo* find_serial_type(uint8_t id) {
  for (uint8_t i = 0; i < SERIAL_TYPE_COUNT; i++) {
    if (SERIAL_TYPES[i].id == id) return &SERIAL_TYPES[i];
  }
  return nullptr;
}

GeigerSerial::GeigerSerial() {
};

void GeigerSerial::begin() {
  uint8_t st = (uint8_t)EGPrefs::getUInt("input", "serial_type");
  const SerialTypeInfo* info = find_serial_type(st);
  if (info) _serial_type = st;
  else      info = find_serial_type(_serial_type);
  uint32_t baud = info ? info->baud : 9600;
  Log::console(PSTR("GeigerSerial: %s (type %d) baud %lu rx %d"),
               info ? info->name : "?", _serial_type, baud, _rx_pin);
  if (_rx_pin == 1 || _rx_pin == 3 || _tx_pin == 1 || _tx_pin == 3) {
    Log::error(PSTR("GeigerSerial: rx/tx pin clashes with UART0"));
  }
  geigerPort.begin(baud, SWSERIAL_8N1, _rx_pin, _tx_pin, false, 64);
}

void GeigerSerial::pullSerial() {
  uint8_t maxRead = sizeof(_serial_buffer);
  while (geigerPort.available() && maxRead--) {
    char input = geigerPort.read();
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
  if (_loop_c <= 5 && _serial_idx == 0) {
    _loop_c++;
    return;
  }
  _loop_c = 0;
  pullSerial();
  if (serial_value <= 0) {
    serial_value = 0;
    return;
  }
  if (millis() - last_serial > 10000) {
    serial_value = 0;
  }
}

void GeigerSerial::secondTicker() {
  // All serial types report CPM - accumulate fractional clicks per second.
  // Multiply by precomputed reciprocal - soft-float divide costs ~150 cycles
  // on ESP8266, multiply only ~80. -Os may not fold this automatically.
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

void GeigerSerial::handleSerial(char* input) {
  size_t inputLen = strlen(input);
  for (size_t x = 0; x < inputLen; x++) {
    if (!isPrintable(input[x]) && input[x] != '\r' && input[x] != '\n') {
      Log::debug(PSTR("Non-printable character on serial"));
      _bad_streak++;
      goto maybe_drain;
    }
  }

  {
    int _scpm = 0;
    int n = 0;
    switch (_serial_type) {
      case GEIGER_STYPE_MIGHTYOHM: {
        int _scps;
        n = sscanf(input, "CPS, %d, CPM, %d", &_scps, &_scpm);
        if (n != 2) { _bad_streak++; goto maybe_drain; }
        break;
      }
      case GEIGER_STYPE_ESPGEIGER:
        n = sscanf(input, "CPM: %d", &_scpm);
        if (n != 1) { _bad_streak++; goto maybe_drain; }
        break;
      default:
        // GC10 / GC10Next - plain digits
        for (size_t x = 0; x < inputLen; x++) {
          if (!isDigit(input[x]) && input[x] != '\r' && input[x] != '\n') {
            _bad_streak++;
            goto maybe_drain;
          }
        }
        n = sscanf(input, "%d", &_scpm);
        if (n != 1) { _bad_streak++; goto maybe_drain; }
        break;
    }

    // 1M CPM is way beyond any realistic tube — reject as garbage.
    if (_scpm < 0 || _scpm > 1000000) {
      Log::debug(PSTR("GeigerSerial: out-of-range CPM %d"), _scpm);
      _bad_streak++;
      goto maybe_drain;
    }

    Log::debug(PSTR("GeigerSerial: Loop - %d"), _scpm);
    setLastBlip();
    serial_value = _scpm;
    last_serial = millis();
    _bad_streak = max((int)_bad_streak - 3, 0);
    return;
  }

maybe_drain:
  if (_bad_streak >= GEIGERSERIAL_BAD_LIMIT) {
    Log::console(PSTR("GeigerSerial: %u bad lines, draining port"), _bad_streak);
    int n = geigerPort.available();
    while (n-- > 0) geigerPort.read();
    _serial_idx = 0;
    _serial_buffer[0] = '\0';
    _bad_streak = 0;
  }
}
