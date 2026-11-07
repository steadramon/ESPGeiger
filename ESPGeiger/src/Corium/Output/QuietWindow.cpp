/*
  QuietWindow.cpp - A daily quiet window that may cross midnight.

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

#include "QuietWindow.h"
#include "Corium/System/FastMillis.h"
#include "Corium/System/Clock.h"

void corium::QuietWindow::set(const char* from, const char* to) {
  _from = quietMinute(from);
  _to = quietMinute(to);
  _recompute_ms = corium::fastMillis();
}

// every call, cached to the minute, localTm() is slow
bool corium::QuietWindow::active() {
  if (_from < 0 || _to < 0 || _from == _to) return false;   // off
  if (!corium::Clock::synced) return false;
  unsigned long now_ms = corium::fastMillis();
  // never more than a minute ahead, so a far one is stale (not asked for weeks)
  if (!corium::reached(now_ms, _recompute_ms) && (uint32_t)(_recompute_ms - now_ms) <= 60000UL) return _quiet;
  struct tm t;
  if (!corium::Clock::localTm(&t)) return false;
  _quiet = quietInWindow(t.tm_hour * 60 + t.tm_min, _from, _to);
  _recompute_ms = now_ms + (60UL - t.tm_sec) * 1000UL;
  return _quiet;
}
