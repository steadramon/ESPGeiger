/*
  GeigerInput/SerialFormat.h - Shared serial protocol format / parse.

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

// Serial counter protocol codec. Shared by GeigerSerial (real external
// devices) and GeigerTestSerial (simulated/loopback). Pure functions.

#ifndef GEIGER_SERIAL_FORMAT_H
#define GEIGER_SERIAL_FORMAT_H

#include <Arduino.h>

// Protocol IDs — stable ints, persisted in prefs. New protocol: append
// a value here + a row in TYPES[] + cases in format_line/parse_cpm.
#define GEIGER_STYPE_GC10       1
#define GEIGER_STYPE_GC10NX     2
#define GEIGER_STYPE_MIGHTYOHM  3
#define GEIGER_STYPE_ESPGEIGER  4

namespace SerialFormat {

size_t      format_line(uint8_t type, int cps, int cpm, char* buf, size_t cap);
bool        parse_cpm(uint8_t type, const char* input, int* out_cpm);

uint32_t    baud_for(uint8_t type);            // 0 if unknown
const char* name_for(uint8_t type);            // nullptr if unknown
bool        is_known(uint8_t type);
size_t      describe_types(char* buf, size_t cap);   // "1=GC10 2=GC10Next ..."

}

#endif
