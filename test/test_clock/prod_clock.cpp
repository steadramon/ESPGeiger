/*
  prod_clock.cpp - Clock.cpp and Uptime.cpp built against a settable time().

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
#include <Arduino.h>
#include <time.h>
#include "clock_fakes.h"
#define time(out) fake_time(out)
#include "Corium/System/Clock.cpp"
#include "Corium/System/Uptime.cpp"

time_t fake_now = 0;
time_t fake_time(time_t* out) { if (out) *out = fake_now; return fake_now; }
long start = 0;
