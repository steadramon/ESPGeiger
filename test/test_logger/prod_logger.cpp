/*
  prod_logger.cpp - the real Logger.cpp and what it reads.

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
#include "log_fakes.h"
#include "Corium/Logger/Logger.cpp"

bool Wifi::disabled = false;
bool Clock::synced = false;
int16_t Clock::tz_offset_min = 0;
UptimeCounter Uptime::counter;
