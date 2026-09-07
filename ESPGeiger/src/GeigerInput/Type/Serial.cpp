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
#include "../../Corium/System/FastMillis.h"

#if GEIGER_IS_SERIAL(GEIGER_TYPE)

#include "../../Corium/Logger/Logger.h"
#include "../../Corium/Prefs/EGPrefs.h"
#include "../../GeigerPrefs/GeigerKeys.h"
#include "../../Counter/Counter.h"
#include "../../Counter/Tube.h"
#include "../SerialFormat.h"

// isr margin at 115200
static GeigerPort<64> geigerPort;

GeigerSerial::GeigerSerial() {
};

void GeigerSerial::begin() {
  uint8_t st = (uint8_t)EGPrefs::getUInt("counter", EG_K_SERIAL_TYPE);
  if (SerialFormat::is_known(st)) _serial_type = st;
  uint32_t    baud = SerialFormat::baud_for(_serial_type);
  const char* name = SerialFormat::name_for(_serial_type);
  if (baud == 0) baud = 9600;    // unknown pref
  _use_cps = false;
  if (name && EGPrefs::getString("counter", EG_K_GEIGER_MODEL)[0] == '\0') {
    Tube::set_model(name);
  }
  Log::console(PSTR("GeigerSerial: %s (type %d) BAUD: %lu RXPIN: %d hasCPS=%d"),
               name ? name : PSTR("?"), _serial_type, baud, _rx_pin,
               SerialFormat::has_cps(_serial_type) ? 1 : 0);
  if (_rx_pin == 1 || _rx_pin == 3 || _tx_pin == 1 || _tx_pin == 3) {
    Log::console(PSTR("GeigerSerial: ERROR rx/tx pin clashes with UART0"));
  }
  if (!geigerPort.begin(baud, _rx_pin, _tx_pin)) {
    Log::console(PSTR("GeigerSerial: ERROR port would not open"));
  }
  geigerPort.flush();
  uint32_t skip = 6000000UL / baud;
  if (skip < 5)   skip = 5;
  if (skip > 500) skip = 500;
  _idle_skip = (uint16_t)skip;
  _poll_skip = _idle_skip;
}

// well over a 64 B line at the slowest baud
#define GEIGERSERIAL_LINE_TIMEOUT_MS 1000

// bad lines in a row before a drain
#define GEIGERSERIAL_BAD_LIMIT 20

// its tail would parse as a line of its own
void GeigerSerial::drop_line() {
  _serial_idx = 0;
  _serial_buffer[0] = '\0';
  _discard = true;
  _bad_streak++;
  if (_bad_streak >= GEIGERSERIAL_BAD_LIMIT) drainPort();
}

void GeigerSerial::pullSerial() {
  static_assert(sizeof(_serial_buffer) < 256,
                "maxRead is uint8_t: a 256-byte buffer truncates it to 0 and stops all input");
  uint8_t maxRead = sizeof(_serial_buffer);
  bool got_any = false;
  while (geigerPort.available() && maxRead--) {
    char input = geigerPort.read();
    got_any = true;
    if (_discard) {
      if (input == '\n') _discard = false;
      continue;
    }
    if (input != '\n' && input != '\r' && !isPrintable((uint8_t)input)) {
      drop_line();
      continue;
    }
    _serial_buffer[_serial_idx++] = input;
    if (input == '\n') {
      _serial_buffer[_serial_idx++] = '\0';
      handleSerial(_serial_buffer);
      _serial_idx = 0;
      _serial_buffer[0] = '\0';
    } else if (_serial_idx >= sizeof(_serial_buffer) - 2) {
      drop_line();   // too long
    }
  }
  if (got_any) _line_ms = fast_millis();
}

void GeigerSerial::loop() {
  // else a stray byte polls every pass
  if ((_serial_idx != 0 || _discard) && (fast_millis() - _line_ms) > GEIGERSERIAL_LINE_TIMEOUT_MS) {
    _serial_idx = 0;
    _serial_buffer[0] = '\0';
    _discard = false;
  }
  pullSerial();
  _poll_skip = (_serial_idx || _discard) ? 0 : _idle_skip;   // mid line, every pass
  if (serial_value <= 0) return;
  if (fast_millis() - last_serial > 10000) serial_value = 0;
}

// saturate, don't wrap
static inline uint16_t clamp_batch(int n) {
  if (n < 0)      return 0;
  if (n > 0xFFFF) return 0xFFFF;
  return (uint16_t)n;
}

void GeigerSerial::stopForOTA() {
  geigerPort.pause();
}

void GeigerSerial::restartAfterOTA() {
  _serial_idx = 0;
  _serial_buffer[0] = '\0';
  _discard = false;
  geigerPort.resume();
}

void GeigerSerial::secondTicker() {
  // sender can toggle cps
  if (_use_cps) {
    int c = (int)partial_clicks;
    partial_clicks = 0;
    setCounter(c, false);
    if (c > 0) Counter::on_pulse_batch(clamp_batch(c), (uint32_t)micros(), 1000000UL);
    _use_cps = false;
    return;
  }
  // no soft-float divide
  static constexpr float INV_60 = 1.0f / 60.0f;
  partial_clicks += (float)serial_value * INV_60;
  if (partial_clicks >= 1.0f) {
    int full_clicks = (int)partial_clicks;
    partial_clicks -= full_clicks;
    setCounter(full_clicks, false);
    Counter::on_pulse_batch(clamp_batch(full_clicks), (uint32_t)micros(), 1000000UL);
  }
}

void GeigerSerial::drainPort() {
  unsigned long now = fast_millis();
  if (now - _last_drain_log > 60000UL) {
    Log::console(PSTR("GeigerSerial: %u bad lines, draining port"), _bad_streak);
    _last_drain_log = now;
  } else {
    Log::debug(PSTR("GeigerSerial: %u bad lines, draining port"), _bad_streak);
  }
  geigerPort.flush();
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
  last_serial = fast_millis();
  _bad_streak = max((int)_bad_streak - 3, 0);
}

#endif // GEIGER_IS_SERIAL
