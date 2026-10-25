/*
  AppKeys.cpp - Pref keys and labels the app modules share.

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
#include "Corium/Platform/Flash.h"
#include "App/AppKeys.h"

const char EG_K_BRIGHTNESS[]   PROGMEM = "brightness";
const char EG_K_CLK[]          PROGMEM = "clk";
const char EG_K_CLKR[]         PROGMEM = "clkr";
const char EG_K_ENABLE[]       PROGMEM = "enable";
const char EG_K_FADE_SHIFT[]   PROGMEM = "fade_shift";
const char EG_K_FBT[]          PROGMEM = "fbt";
const char EG_K_FREQ[]         PROGMEM = "freq";
const char EG_K_INTERVAL[]     PROGMEM = "interval";
const char EG_K_LAT[]          PROGMEM = "lat";
const char EG_K_LON[]          PROGMEM = "lon";
const char EG_K_MODE[]         PROGMEM = "mode";
const char EG_K_PASSWORD[]     PROGMEM = "password";
const char EG_K_PIN[]          PROGMEM = "pin";
const char EG_K_POLARITY[]     PROGMEM = "polarity";
const char EG_K_PORT[]         PROGMEM = "port";
const char EG_K_PULSE_MS[]     PROGMEM = "pulse_ms";
const char EG_K_QUIET_FROM[]   PROGMEM = "quiet_from";
const char EG_K_QUIET_TO[]     PROGMEM = "quiet_to";
const char EG_K_RATIO[]        PROGMEM = "ratio";
const char EG_K_SECS[]         PROGMEM = "secs";
const char EG_K_SEND[]         PROGMEM = "send";
const char EG_K_TPL[]          PROGMEM = "tpl";
const char EG_K_TYPE[]         PROGMEM = "type";
const char EG_K_USER[]         PROGMEM = "user";
const char EG_K_VOLUME[]       PROGMEM = "volume";
const char EG_L_EN[]  PROGMEM = "Enable";
const char EG_L_MOD[] PROGMEM = "Mode";
const char EG_L_INT[] PROGMEM = "Interval";
const char EG_L_PRT[] PROGMEM = "Port";
const char EG_L_PWD[] PROGMEM = "Password";
const char EG_L_PIN[] PROGMEM = "GPIO";
const char EG_L_POL[] PROGMEM = "Polarity";
const char EG_O_POL[] PROGMEM = "Active high|Active low";
const char EG_L_PUL[] PROGMEM = "Pulse width ms";
const char EG_H_PUL[] PROGMEM = "1-50";
const char EG_L_FAD[] PROGMEM = "Fade rate";
const char EG_O_FAD[] PROGMEM = "Fast|Medium|Slow";
const char EG_L_BRT[] PROGMEM = "Brightness";
const char EG_L_VOL[] PROGMEM = "Volume";
const char EG_H_VOL[] PROGMEM = "0-100";
const char EG_L_QFR[] PROGMEM = "Quiet from";
const char EG_L_QTO[] PROGMEM = "Quiet to";
const char EG_H_QTO[] PROGMEM = "End of quiet window; crosses midnight if from > to";
const char EG_P_IP[]  PROGMEM = "[0-9.]+";
