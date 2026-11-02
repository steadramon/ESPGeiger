/*
  Rtc.h - a battery clock on the i2c bus: the time at power-up, kept by NTP.

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
#ifndef CORIUM_RTC_H
#define CORIUM_RTC_H

#include "App/AppConfig.h"
#ifdef CORIUM_RTC

#include <Arduino.h>

namespace Rtc {
  PGM_P chip();   // nullptr = none found
}

#endif
#endif
