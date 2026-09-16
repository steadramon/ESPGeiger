/*
  Features.cpp - Build feature bits for WebAPI "fl".

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
#include "Features.h"
#include "../Corium/System/Globals.h"

// add bits only, never renumber
uint16_t Features::flags() {
  uint16_t f = 0;
#ifdef MQTTOUT
  f |= 1 << 0;
#endif
#ifdef RADMONOUT
  f |= 1 << 1;
#endif
#ifdef THINGSPEAKOUT
  f |= 1 << 2;
#endif
#ifdef WEBHOOKOUT
  f |= 1 << 3;
#endif
#ifdef SERIALOUT
  f |= 1 << 4;
#endif
#ifdef GMCOUT
  f |= 1 << 5;
#endif
#ifdef SSD1306_DISPLAY
  f |= 1 << 6;
#endif
#ifdef GEIGER_PUSHBUTTON
  f |= 1 << 7;
#endif
#ifdef GEIGER_SDCARD
  f |= 1 << 8;
#endif
#ifdef GEIGER_NEOPIXEL
  f |= 1 << 9;
#endif
#ifdef ESPG_HV
  f |= 1 << 10;
#endif
  return f;
}
