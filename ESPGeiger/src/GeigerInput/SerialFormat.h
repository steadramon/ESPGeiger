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
*/

// Serial counter protocol codec. Shared by GeigerSerial (real external
// devices), GeigerTestSerial (simulated/loopback), and SerialOut. Each
// protocol declares an output template plus a parser; only MightyOhm needs
// a custom formatter because of its CPS-dependent mode tag.

#ifndef GEIGER_SERIAL_FORMAT_H
#define GEIGER_SERIAL_FORMAT_H

#include <Arduino.h>

// Protocol IDs - stable ints, persisted in prefs. New protocol: append a
// value here + a row in TYPES[] (declare template + parser).
#define GEIGER_STYPE_GC10       1
#define GEIGER_STYPE_GC10NX     2
#define GEIGER_STYPE_MIGHTYOHM  3
#define GEIGER_STYPE_ESPGEIGER  4
#define GEIGER_STYPE_TEMPLATE   5

namespace SerialFormat {

// Output: protocol formatter resolved once at config-change time. Custom
// formatters (currently MightyOhm + user-template) bypass the template
// engine entirely. The dispatcher returned by resolve() pulls live values
// directly from gcounter / OutputVars so callers don't pass them in.
typedef size_t (*FormatFn)(char* buf, size_t cap);
FormatFn    resolve(uint8_t type);

// Single-shot format - convenient one-off path. Equivalent to
// resolve(type) then calling the result.
size_t      format_line(uint8_t type, char* buf, size_t cap);

// Input: parse a CPM line emitted by the named protocol. out_cps is
// populated only for protocols whose wire carries a per-second count
// (MightyOhm); other protocols leave it untouched.
typedef bool (*ParseFn)(const char* input, int* out_cpm, int* out_cps);
bool        parse_cpm(uint8_t type, const char* input, int* out_cpm, int* out_cps = nullptr);

uint32_t    baud_for(uint8_t type);            // 0 if unknown
const char* name_for(uint8_t type);            // nullptr if unknown
bool        is_known(uint8_t type);
bool        has_cps(uint8_t type);             // true if protocol emits per-second count
size_t      describe_types(char* buf, size_t cap);   // "1=GC10 2=GC10Next ..."

}

#endif
