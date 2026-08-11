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

// Split out of SerialFormat.cpp so these can be host tested. They take a
// const char* and fill two ints: no Counter, no EGPrefs, no OutputVars, which
// is what the formatters in that file drag in.
//
// This is untrusted input off a wire, so treat every bound here as load
// bearing. A value that escapes one goes straight into
// Counter::on_pulse_batch.

#ifndef GEIGER_SERIAL_PARSE_H
#define GEIGER_SERIAL_PARSE_H

#include <stddef.h>

namespace SerialFormat {

// Largest count any protocol may report. Above this the line is rejected
// rather than clamped: a plausible-looking number out of a garbled line is
// worse than no reading.
static const int SERIAL_MAX_COUNT = 1000000;

// Printable plus CR/LF over the whole line. Runs before any protocol parser.
bool common_validate(const char* in, size_t len);

bool parse_gc10(const char* in, int* out_cpm, int* out_cps);
bool parse_mightyohm(const char* in, int* out_cpm, int* out_cps);
bool parse_template(const char* in, int* out_cpm, int* out_cps);

}

#endif
