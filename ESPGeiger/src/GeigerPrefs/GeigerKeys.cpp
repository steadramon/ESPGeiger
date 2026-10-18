/*
  GeigerKeys.cpp - Pref keys only the counter side uses.

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
#include "GeigerKeys.h"

const char EG_K_GEIGER_MODEL[] PROGMEM = "geiger_model";
const char EG_K_SERIAL_TYPE[]  PROGMEM = "serial_type";
const char EG_K_TUBE_ALPHA[]   PROGMEM = "tube_alpha";
const char EG_K_TUBE_BETA[]    PROGMEM = "tube_beta";
const char EG_K_TUBE_GAMMA[]   PROGMEM = "tube_gamma";
const char EG_K_UDPRX_CHIPID[] PROGMEM = "udprx_chipid";
const char EG_K_UDPRX_GROUP[]  PROGMEM = "udprx_group";
const char EG_K_UDPRX_MODE[]   PROGMEM = "udprx_mode";
const char EG_K_UDPRX_PORT[]   PROGMEM = "udprx_port";
const char EG_K_UDPRX_RXMODE[] PROGMEM = "udprx_rxmode";
