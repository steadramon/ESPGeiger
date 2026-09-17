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

#ifndef GEIGER_SERIAL_FORMAT_H
#define GEIGER_SERIAL_FORMAT_H

#include <Arduino.h>

// stored in prefs, append only
#define GEIGER_STYPE_GC10       1
#define GEIGER_STYPE_GC10NX     2
#define GEIGER_STYPE_MIGHTYOHM  3
#define GEIGER_STYPE_ESPGEIGER  4
#define GEIGER_STYPE_TEMPLATE   5

namespace SerialFormat {

typedef size_t (*FormatFn)(char* buf, size_t cap);

// direct, so pulse builds drop TYPES
size_t      fmt_mightyohm(char* buf, size_t cap);
size_t      fmt_user_template(char* buf, size_t cap);

const char* mohm_mode(int cps);   // flash

size_t      format_line(uint8_t type, char* buf, size_t cap);

// out_cps only set by MightyOhm
typedef bool (*ParseFn)(const char* input, int* out_cpm, int* out_cps);
bool        parse_cpm(uint8_t type, const char* input, int* out_cpm, int* out_cps = nullptr);

uint32_t    baud_for(uint8_t type);            // 0 if unknown
const char* name_for(uint8_t type);            // flash, nullptr if unknown
bool        is_known(uint8_t type);
bool        has_cps(uint8_t type);
extern const char TYPE_OPTS[];                 // flash, ids from 1

}

#endif
