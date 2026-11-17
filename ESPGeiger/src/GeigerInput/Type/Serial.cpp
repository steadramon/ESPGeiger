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
#include "Corium/System/FastMillis.h"

#if GEIGER_TYPE == GEIGER_TYPE_SERIAL

#include "Corium/Logger/Logger.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/Util/MathUtil.h"
#include "GeigerPrefs/GeigerKeys.h"
#include "Counter/Counter.h"
#include "Counter/Tube.h"
#include "GeigerInput/SerialFormat.h"

// isr margin at 115200
static GeigerPort<64> s_port;

GeigerSerial::GeigerSerial() {
};

void GeigerSerial::begin() {
  uint8_t st = (uint8_t)corium::Prefs::getUInt("counter", EG_K_SERIAL_TYPE);
  if (SerialFormat::is_known(st)) _serial_type = st;
  uint32_t    baud = SerialFormat::baud_for(_serial_type);
  const char* name = SerialFormat::name_for(_serial_type);
  if (baud == 0) baud = 9600;    // unknown pref
  _use_cps = false;
  if (name && corium::Prefs::getString("counter", EG_K_GEIGER_MODEL)[0] == '\0') {
    Tube::set_model(name);
  }
  corium::Log::console(PSTR("GeigerSerial: %s (type %d) BAUD: %lu RXPIN: %d hasCPS=%d"),
               name ? name : PSTR("?"), _serial_type, baud, _rx_pin,
               SerialFormat::has_cps(_serial_type) ? 1 : 0);
  if (!s_port.begin(baud, _rx_pin, _tx_pin)) {
    corium::Log::console(PSTR("GeigerSerial: ERROR port would not open"));
  }
  s_port.flush();
  _idle_ms = (uint8_t)clamp<uint32_t>(50000UL / baud, 1, 4);   // ~5 chars
  _polls = true;
  _line.mark(corium::fastMillis());   // no line yet isn't no input, the wait starts here
}

// well over a 64 B line at the slowest baud
#define GEIGERSERIAL_LINE_TIMEOUT_MS 1000

// bad lines in a row before a drain
#define GEIGERSERIAL_BAD_LIMIT 20

// connected: a good line in this long, and no drain
#define GEIGERSERIAL_OK_MS 60000

// sender quiet this long: cpm 0, cps off
#define GEIGERSERIAL_QUIET_MS 10000

inline __attribute__((always_inline)) void GeigerSerial::bad_line() {
  _bad_streak++;
  if (_bad_streak >= GEIGERSERIAL_BAD_LIMIT) drainPort();
}

// its tail would parse as a line of its own
void GeigerSerial::drop_line() {
  clear_line();
  _discard = true;
  bad_line();
}

void GeigerSerial::pullSerial() {
  static_assert(sizeof(_serial_buffer) < 256,
                "maxRead is uint8_t: a 256-byte buffer truncates it to 0 and stops all input");
  uint8_t maxRead = sizeof(_serial_buffer);
  bool got_any = false;
  while (s_port.available() && maxRead--) {
    char input = s_port.read();
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
      clear_line();
    } else if (_serial_idx >= sizeof(_serial_buffer) - 2) {
      drop_line();   // too long
    }
  }
  if (got_any) _rx_ms = corium::fastMillis();
}

void GeigerSerial::loop() {
  // else a stray byte polls every pass
  if ((_serial_idx != 0 || _discard) && (corium::fastMillis() - _rx_ms) > GEIGERSERIAL_LINE_TIMEOUT_MS) {
    clear_line();
    _discard = false;
  }
  pullSerial();
  uint32_t now = corium::fastMillis();
  _poll_at = (_serial_idx || _discard) ? now : now + _idle_ms;   // mid line, every pass
  bool line = _line.fresh(now, GEIGERSERIAL_OK_MS);
  _ok = !_drain.fresh(now, GEIGERSERIAL_OK_MS) && line;
  if (_cpm <= 0) return;
  if (corium::fastMillis() - _good_ms > GEIGERSERIAL_QUIET_MS) _cpm = 0;
}

// saturate, don't wrap
static inline uint16_t clamp_batch(int n) {
  if (n < 0)      return 0;
  if (n > 0xFFFF) return 0xFFFF;
  return (uint16_t)n;
}

void GeigerSerial::stopForOTA() {
  s_port.pause();
}

void GeigerSerial::restartAfterOTA() {
  clear_line();
  _discard = false;
  s_port.resume();
}

void GeigerSerial::secondTicker() {
  // sender can toggle cps
  if (_use_cps) {
    int c = (int)_carry;
    _carry = 0;
    setCounter(c, false);
    if (c > 0) Counter::on_pulse_batch(clamp_batch(c), (uint32_t)micros(), 1000000UL);
    _use_cps = false;
    return;
  }
  // cps sender, its line comes next tick
  if (_cps_seen.fresh(corium::fastMillis(), GEIGERSERIAL_QUIET_MS)) return;
  // cpm/60 a second, fraction carried; no soft-float divide
  static constexpr float INV_60 = 1.0f / 60.0f;
  _carry += (float)_cpm * INV_60;
  if (_carry >= 1.0f) {
    int full_clicks = (int)_carry;
    _carry -= full_clicks;
    setCounter(full_clicks, false);
    Counter::on_pulse_batch(clamp_batch(full_clicks), (uint32_t)micros(), 1000000UL);
  }
}

void GeigerSerial::drainPort() {
  static const char DRAIN[] PROGMEM = "GeigerSerial: %u bad lines, draining port";
  unsigned long now = corium::fastMillis();
  if (now - _last_drain_log > 60000UL) {
    corium::Log::console(DRAIN, _bad_streak);
    _last_drain_log = now;
  } else {
    corium::Log::debug(DRAIN, _bad_streak);
  }
  s_port.flush();
  clear_line();
  _bad_streak = 0;
  _drain.mark(now);
}

void GeigerSerial::handleSerial(char* input) {
  int cpm = 0;
  int cps = -1;
  if (!SerialFormat::parse_cpm(_serial_type, input, &cpm, &cps)) {
    bad_line();
    return;
  }

  corium::Log::debug(PSTR("GeigerSerial: Loop - %d"), cpm);
  setLastBlip();
  _cpm = cpm;
  _good_ms = corium::fastMillis();
  if (cps >= 0) {
    _carry += (float)cps;
    _use_cps = true;
    _cps_seen.mark(_good_ms);
  }
  _line.mark(_good_ms);
  // a good line takes 3 off
  _bad_streak = max((int)_bad_streak - 3, 0);
}

#endif // GEIGER_TYPE_SERIAL
