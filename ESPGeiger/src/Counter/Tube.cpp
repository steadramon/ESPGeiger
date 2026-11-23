/*
  Tube.cpp - Tube model and what it detects.

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
#include "Tube.h"
#include "Corium/Prefs/Prefs.h"
#include "GeigerPrefs/GeigerKeys.h"

#ifndef GEIGER_MODEL
#define GEIGER_MODEL ""
#endif

static char s_model[CORIUM_NAME_MAX + 1] = GEIGER_MODEL;

const char* Tube::model() { return s_model; }

void Tube::set_model(const char* s) {
  if (s && pgm_read_byte(s)) {
    strncpy_P(s_model, s, sizeof(s_model) - 1);
  } else {
    strncpy(s_model, GEIGER_MODEL, sizeof(s_model) - 1);
  }
  s_model[sizeof(s_model) - 1] = '\0';
}

uint8_t Tube::detection() {
  uint8_t f = 0;
  if (corium::Prefs::getBool("counter", EG_K_TUBE_ALPHA)) f |= 0x01;
  if (corium::Prefs::getBool("counter", EG_K_TUBE_BETA))  f |= 0x02;
  if (corium::Prefs::getBool("counter", EG_K_TUBE_GAMMA)) f |= 0x04;
  return f;
}

const char TUBE_OPTS[] PROGMEM = "Other|SBM-20|SBM-19|SBM-21|STS-5|SI-22G|J305|J321|M4011|LND 712|VacuTec 70 013";

uint8_t Tube::id() {
  uint32_t t = corium::Prefs::getUInt("counter", EG_K_TUBE);
  return t < COUNT ? (uint8_t)t : 0;
}
