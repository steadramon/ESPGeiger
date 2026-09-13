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
#include "../Corium/Prefs/EGPrefs.h"
#include "../GeigerPrefs/GeigerKeys.h"

#ifndef GEIGER_MODEL
#define GEIGER_MODEL ""
#endif

static char s_model[EG_NAME_MAX + 1] = GEIGER_MODEL;

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
  if (EGPrefs::getBool("counter", EG_K_TUBE_ALPHA)) f |= 0x01;
  if (EGPrefs::getBool("counter", EG_K_TUBE_BETA))  f |= 0x02;
  if (EGPrefs::getBool("counter", EG_K_TUBE_GAMMA)) f |= 0x04;
  return f;
}
