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

void QuietWindow::set(const char* from, const char* to) {
  _from = quiet_minute(from);
  _to = quiet_minute(to);
  _recompute_ms = fast_millis();
}

// every click, cached to the minute, localTm() is slow
bool QuietWindow::active() {
  if (_from < 0 || _to < 0 || _from == _to) return false;   // off
  if (!Clock::synced) return false;
  unsigned long now_ms = fast_millis();
  if ((long)(now_ms - _recompute_ms) < 0) return _cached;
  struct tm t;
  if (!Clock::localTm(&t)) return false;
  _cached = quiet_in_window(t.tm_hour * 60 + t.tm_min, _from, _to);
  _recompute_ms = now_ms + (60UL - t.tm_sec) * 1000UL;
  return _cached;
}
