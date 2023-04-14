/*
  NTP.h - NTP class
  
  Copyright (C) 2023 @steadramon

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
#ifndef _NTP_h
#define _NTP_h
#include <Arduino.h>
#include <ESPNtpClient.h>
#include "../Status.h"

#ifndef NTP_SERVER
#  define NTP_SERVER "pool.ntp.org"
#endif

extern Status status;

void setupNTP();

#endif