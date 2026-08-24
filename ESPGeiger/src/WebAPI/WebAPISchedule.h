/*
  WebAPISchedule.h - next-attempt clock for the WebAPI handshake.

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

#ifndef ESPGEIGER_WEBAPISCHEDULE_H
#define ESPGEIGER_WEBAPISCHEDULE_H

#include <stdint.h>

// Cross-task field qualifier, same rule as EGModule.h: the reply callback runs
// on the AsyncTCP task on ESP32 and in the main thread on ESP8266. Defined here
// too so the host suite can include this header on its own.
#ifndef EG_XTASK_VOLATILE
#ifdef ESP32
#define EG_XTASK_VOLATILE volatile
#else
#define EG_XTASK_VOLATILE
#endif
#endif

// When the handshake may next be attempted. Split out of WebAPI so the host
// suite can reach it: the module around it needs LittleFS, uECC and the HTTP
// client.
//
// Every attempt must advance the arm before it can fail. A handshake left due
// spins the module at the registry's 100ms floor, signing a request per pass.
//
// Deltas are compared signed: retry() can push the arm past now + interval,
// where an unsigned compare underflows into permanently due.
//
// Types are uint32_t, not `unsigned long`: same width on target, 64-bit on a
// host, where the wrap cases would silently never fire.
class WebAPISchedule {
public:
  static constexpr uint32_t BACKOFF_MIN_MS = 30000UL;
  static constexpr uint32_t BACKOFF_MAX_MS = 5UL * 60UL * 1000UL;

  explicit WebAPISchedule(uint32_t interval_ms) : _interval(interval_ms) {}

  // 0 means never armed. Reachable as a real answer from the arithmetic below,
  // so every writer nudges past it rather than carrying a second flag.
  bool anchored() const { return _last != 0; }

  // First arm of the session. offset_ms spreads devices across the interval.
  void anchor(uint32_t now, uint32_t offset_ms) { set(now + offset_ms - _interval); }

  // 403: station is gone, re-anchor on the next pass through loop().
  void unanchor() { _last = 0; }

  bool due(uint32_t now) const {
    return (int32_t)(now - _last) >= (int32_t)_interval;
  }

  // Any failure. Retry after the current backoff, then widen it.
  void retry(uint32_t now) {
    set(now - _interval + _backoff);
    uint32_t next = _backoff * 2;
    _backoff = (next > BACKOFF_MAX_MS) ? BACKOFF_MAX_MS : next;
  }

  // The request left the device. Nothing is due until the reply moves the arm
  // or a full interval passes, whichever comes first.
  void sent(uint32_t now) { set(now); }

  // Accepted, and the station id is new: re-anchor onto its slot.
  void accepted(uint32_t now, uint32_t slot_wait_ms) {
    set(now + slot_wait_ms - _interval);
    reset_backoff();
  }

  // Accepted on the slot we already hold. The arm stands; only the widened
  // retry interval is stale.
  void reset_backoff() { _backoff = BACKOFF_MIN_MS; }

  // For EGModuleRegistry::sleep_until.
  uint32_t target() const { return _last + _interval; }

  uint32_t backoff_ms() const { return _backoff; }

private:
  void set(uint32_t at) { _last = at ? at : 1; }

  const uint32_t _interval;
  EG_XTASK_VOLATILE uint32_t _last    = 0;
  EG_XTASK_VOLATILE uint32_t _backoff = BACKOFF_MIN_MS;
};

// Milliseconds until this station's slot within a period_s wall-clock cycle,
// so a fleet spreads its handshakes instead of arriving together on the hour.
// k is any per-device value; epoch_s is time(NULL).
//
// epoch_s is cast to uint32_t by the caller: signed mod on a negative time_t
// (post-2038) wraps cur_ms.
static inline uint32_t wall_clock_wait_ms(uint32_t k, uint32_t interval_ms,
                                          uint32_t period_s, uint32_t epoch_s) {
  uint32_t target_ms = k % interval_ms;
  uint32_t cur_ms    = (epoch_s % period_s) * 1000UL;
  return (target_ms >= cur_ms) ? target_ms - cur_ms
                               : interval_ms - cur_ms + target_ms;
}

#endif
