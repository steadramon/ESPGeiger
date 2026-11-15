/*
  GeigerInput/SerialParse.h - serial protocol parsers.

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

// Host tested: no Counter, Prefs or OutputVars
// Untrusted wire input, every bound matters: it goes on into the counter

#ifndef GEIGER_SERIAL_PARSE_H
#define GEIGER_SERIAL_PARSE_H

#include <stddef.h>

namespace SerialFormat {

// Over this the line is dropped, not clamped: no reading beats a garbled one
static const int SERIAL_MAX_COUNT = 10000 * 60;   // cpm, a GM tube tops out near 10k cps

// Printable plus CR/LF, before any parser
bool common_validate(const char* in, size_t len);

bool parse_gc10(const char* in, int* out_cpm, int* out_cps);
bool parse_mightyohm(const char* in, int* out_cpm, int* out_cps);
bool parse_espgeiger(const char* in, int* out_cpm, int* out_cps);
bool parse_template(const char* in, int* out_cpm, int* out_cps);

}

#endif
