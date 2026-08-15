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
#include "../../Util/FastMillis.h"

#if GEIGER_IS_SERIAL(GEIGER_TYPE)

#include "../../Logger/Logger.h"
#include "../../Prefs/EGPrefs.h"
#include "../../Util/DeviceInfo.h"
#include "../../Util/HangWatch.h"
#include "../../Counter/Counter.h"   // Counter::on_pulse_batch ring synth (serial CPM/CPS)
#include "../SerialFormat.h"

// 64-byte RX buffer at 115200 (GC10NX) gives ISR margin under WiFi PHY work.
static GeigerPort<64> geigerPort;

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
  if (!geigerPort.begin(baud, _rx_pin, _tx_pin)) {
    Log::console(PSTR("GeigerSerial: ERROR port would not open"));
  }
  for (uint16_t i = 0; i < 256 && geigerPort.available(); i++) geigerPort.read();
  uint32_t skip = 6000000UL / baud;
  if (skip < 5)   skip = 5;
  if (skip > 500) skip = 500;
  _poll_skip = (uint16_t)skip;
}

// A partial line no longer counts as activity after this, and is discarded.
// Well clear of a full 64-byte line at the slowest supported baud (~67 ms).
#define GEIGERSERIAL_LINE_TIMEOUT_MS 1000

// TEMPORARY - GC10Next hardware-watchdog hunt. Remove with the matching rows
// in WebPortal's /info handler.
namespace GeigerSerialDiag {
  uint32_t      ovf        = 0;   // decoder outrun since boot
  uint32_t      buf_ovf    = 0;   // byte-buffer overruns since boot
  uint32_t      isr_drops  = 0;   // declined or muted by the admission budget
  uint32_t      framing    = 0;   // EGTinySerial only
  uint32_t      coalesced  = 0;   // EGTinySerial only
  uint32_t      breaks     = 0;   // EGTinySerial only
  uint32_t      rx_bytes   = 0;   // EGTinySerial only
  uint32_t      stalls     = 0;   // EGTinySerial only
  uint32_t      isr_calls  = 0;   // EG_TINYSERIAL_BENCH only
  uint32_t      isr_max    = 0;   // EG_TINYSERIAL_BENCH only
  uint32_t      lines_ok   = 0;   // lines that parsed
  uint8_t       bad_peak   = 0;   // worst consecutive unparseable lines
  uint8_t       max_bytes  = 0;   // most bytes taken in one pullSerial call
  unsigned long last_drain = 0;
  uint32_t      drains     = 0;
}

void GeigerSerial::pullSerial() {
  static_assert(sizeof(_serial_buffer) < 256,
                "maxRead is uint8_t: a 256-byte buffer truncates it to 0 and stops all input");
  uint8_t maxRead = sizeof(_serial_buffer);
  bool got_any = false;
  uint8_t took = 0;                     // TEMPORARY diag
  while (geigerPort.available() && maxRead--) {
    char input = geigerPort.read();
    got_any = true;
    took++;
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
  if (got_any) _line_ms = fast_millis();
  // TEMPORARY diag. The port's counters are monotonic, so copy rather than
  // accumulate. pollStats() is where a read-and-clear backend samples.
  geigerPort.pollStats();
  const GeigerPortStats ps = geigerPort.stats();
  GeigerSerialDiag::ovf       = ps.isr_ovf;
  GeigerSerialDiag::buf_ovf   = ps.buf_ovf;
  GeigerSerialDiag::isr_drops = ps.declined;
  GeigerSerialDiag::framing   = ps.framing;
  GeigerSerialDiag::coalesced = ps.coalesced;
  GeigerSerialDiag::breaks    = ps.breaks;
  GeigerSerialDiag::rx_bytes  = ps.bytes;
  GeigerSerialDiag::stalls    = ps.stalls;
  GeigerSerialDiag::isr_calls = ps.isr_calls;
  GeigerSerialDiag::isr_max   = ps.isr_max;
  if (took > GeigerSerialDiag::max_bytes) GeigerSerialDiag::max_bytes = took;
}

void GeigerSerial::loop() {
  // Drop a line that stopped arriving. The throttle below is skipped while a
  // line is part-read, so a stray byte with no terminator would pin
  // _serial_idx non-zero and poll every iteration. Nothing else clears it:
  // the 10 s timeout resets serial_value, and drainPort needs bad lines.
  if (_serial_idx != 0 && (fast_millis() - _line_ms) > GEIGERSERIAL_LINE_TIMEOUT_MS) {
    _serial_idx = 0;
    _serial_buffer[0] = '\0';
  }
  if (_loop_c < _poll_skip && _serial_idx == 0) {
    _loop_c++;
    return;
  }
  _loop_c = 0;
  pullSerial();
  if (serial_value <= 0) return;
  if (fast_millis() - last_serial > 10000) serial_value = 0;
}

// Counter::on_pulse_batch is uint16_t wide. Saturating keeps a wild value
// looking wild instead of wrapping it into the normal range.
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
  geigerPort.resume();
}

void GeigerSerial::secondTicker() {
  geigerPort.tick();   // re-derives the bit period if the CPU clock moved
  // Into RTC, so the values survive a hardware watchdog. Nothing else about
  // the hang does.
  HangWatch::set_serial(GeigerSerialDiag::isr_calls, GeigerSerialDiag::isr_max,
                        GeigerSerialDiag::isr_drops, GeigerSerialDiag::coalesced);
  // _use_cps is a per-second flag (set in handleSerial when wire CPS
  // arrived, reset here) so a producer can toggle `show cps` mid-run.
  if (_use_cps) {
    int c = (int)partial_clicks;
    partial_clicks = 0;
    setCounter(c, false);
    // on_pulse_batch takes uint16_t. A plain cast wraps: 1000000 arrives as
    // 16960, which is small and plausible and therefore invisible. Saturate.
    if (c > 0) Counter::on_pulse_batch(clamp_batch(c), (uint32_t)micros(), 1000000UL);
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
    Counter::on_pulse_batch(clamp_batch(full_clicks), (uint32_t)micros(), 1000000UL);
  }
}

// Reset SoftSerial if we see this many consecutive un-parseable lines.
#define GEIGERSERIAL_BAD_LIMIT 20

void GeigerSerial::drainPort() {
  unsigned long now = fast_millis();
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
  GeigerSerialDiag::last_drain = now;   // TEMPORARY diag
  GeigerSerialDiag::drains++;
}

void GeigerSerial::handleSerial(char* input) {
  int _scpm = 0;
  int _scps = -1;
  if (!SerialFormat::parse_cpm(_serial_type, input, &_scpm, &_scps)) {
    _bad_streak++;
    if (_bad_streak > GeigerSerialDiag::bad_peak) GeigerSerialDiag::bad_peak = _bad_streak;  // TEMPORARY diag
    if (_bad_streak >= GEIGERSERIAL_BAD_LIMIT) drainPort();
    return;
  }

  GeigerSerialDiag::lines_ok++;
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
