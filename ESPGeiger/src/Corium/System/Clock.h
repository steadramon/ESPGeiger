/*
  Clock.h - wall-clock time, set by whichever source syncs it.

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

#ifndef CORIUM_CLOCK_H
#define CORIUM_CLOCK_H

#include <Arduino.h>
#include <time.h>
#include "Corium/System/FastMillis.h"

namespace corium {
namespace Clock {
  extern bool synced;
  extern time_t bootEpoch;

  // Cached, localtime is slow
  extern int16_t tzOffsetMin;
  void refreshTzOffsetMin();

  // Who set it. RTC and the browser only set it while nothing has, NTP always does
  enum Source : uint8_t { NONE, KEPT, RTC, WEB, NTP };
  extern Source source;
  // Loop task only, a sync callback posts its module to call this
  bool onSync(Source s = NTP);   // true when s wasn't the last to set it
  void set(time_t epoch, Source s);   // NTP sets the time itself
  void kept();   // at boot, the esp32 keeps time() over a restart
  void stamp();  // esp32, each second, writes what kept() checks against
  void stampNow();   // for the setup portal, stamps time() itself
  void anchor();   // re-anchors a clock set before Reactor::begin

  // A sync off by more than this calls the onStep hook
  static const int32_t STEP_S = 60;
  using StepFn = void (*)(int32_t delta_s);
  void onStep(StepFn fn);

  // Resync at most every 5 min
  extern bool resync;
  extern unsigned long resyncMs;
  inline void requestResync() { resync = true; }
  bool resyncDue(unsigned long now);

  // Wraps in 2106, so only use it for differences and take dates from time()
  inline uint32_t epoch() { return (uint32_t)time(NULL); }
  inline time_t now() { return time(NULL); }   // full width, past 2106 where time_t is 64-bit

  // 2001, anything below = never set. Compared against time_t
  static const uint32_t MIN_EPOCH = 1000000000UL;

  // __DATE__ to midnight UTC. C++11 constexpr, so one return each
  constexpr int dateNum(char c) { return c == ' ' ? 0 : c - '0'; }
  constexpr int dateMonth(const char* d) {
    return d[0] == 'J' ? (d[1] == 'a' ? 0 : d[2] == 'n' ? 5 : 6) : d[0] == 'F' ? 1 :
           d[0] == 'M' ? (d[2] == 'r' ? 2 : 4) : d[0] == 'A' ? (d[1] == 'p' ? 3 : 7) :
           d[0] == 'S' ? 8 : d[0] == 'O' ? 9 : d[0] == 'N' ? 10 : 11;
  }
  constexpr long leapsTo(long y) { return y / 4 - y / 100 + y / 400; }
  constexpr bool leapYear(long y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
  constexpr int MONTH_START[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  constexpr long dayNum(long y, int m, int day) {
    return (y - 1970) * 365 + leapsTo(y - 1) - leapsTo(1969) + MONTH_START[m] + (m > 1 && leapYear(y)) + day - 1;
  }
  constexpr time_t dateEpoch(const char* d) {
    return (time_t)dayNum(dateNum(d[7]) * 1000 + dateNum(d[8]) * 100 + dateNum(d[9]) * 10 + dateNum(d[10]),
                          dateMonth(d), dateNum(d[4]) * 10 + dateNum(d[5])) * 24 * 60 * 60;
  }
#ifndef CORIUM_BUILT_EPOCH
#define CORIUM_BUILT_EPOCH corium::Clock::dateEpoch(__DATE__)
#endif
  // The build's day. A battery clock or kept time before it means a flat cell or garbage
  static const time_t BUILT_EPOCH = CORIUM_BUILT_EPOCH;

  // end of 9999, or what time_t holds (esp32: 2038)
  static const uint64_t MAX_EPOCH = sizeof(time_t) >= 8 ? 253402300799ULL : 2147483647ULL;

  // seconds into a period of wall time, uses the full time_t since 2^32 s isn't whole periods
  inline uint32_t phase(uint32_t period_s) { return period_s ? (uint32_t)(time(NULL) % period_s) : 0; }

  inline bool utcTm(struct tm* out) {
    if (!synced) return false;
    time_t now = time(NULL);
    gmtime_r(&now, out);
    return true;
  }

  inline bool localTm(struct tm* out) {
    if (!synced) return false;
    time_t local = time(NULL) + (time_t)tzOffsetMin * 60;
    gmtime_r(&local, out);
    return true;
  }
}
}  // namespace corium

#endif
