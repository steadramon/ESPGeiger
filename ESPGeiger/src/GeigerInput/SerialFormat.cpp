/*
  GeigerInput/SerialFormat.cpp - Shared serial protocol format / parse.

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

#include "SerialFormat.h"
#include "../Util/StringUtil.h"
#include <string.h>

namespace SerialFormat {

static constexpr float MIGHTYOHM_USV_DIV = 175.0f;

struct TypeInfo {
  uint8_t     id;
  uint32_t    baud;
  const char* name;
  bool        has_cps;   // protocol transmits a usable per-second count
};

static const TypeInfo TYPES[] = {
  { GEIGER_STYPE_GC10,      9600,   "GC10",      false },
  { GEIGER_STYPE_GC10NX,    115200, "GC10Next",  false },
  { GEIGER_STYPE_MIGHTYOHM, 9600,   "MightyOhm", true  },
  { GEIGER_STYPE_ESPGEIGER, 115200, "ESPGeiger", false },
};
static constexpr uint8_t TYPE_COUNT = sizeof(TYPES) / sizeof(TYPES[0]);

static const TypeInfo* find(uint8_t id) {
  for (uint8_t i = 0; i < TYPE_COUNT; i++) {
    if (TYPES[i].id == id) return &TYPES[i];
  }
  return nullptr;
}

uint32_t    baud_for(uint8_t type)  { const TypeInfo* t = find(type); return t ? t->baud : 0; }
const char* name_for(uint8_t type)  { const TypeInfo* t = find(type); return t ? t->name : nullptr; }
bool        is_known(uint8_t type)  { return find(type) != nullptr; }
bool        has_cps(uint8_t type)   { const TypeInfo* t = find(type); return t ? t->has_cps : false; }

size_t describe_types(char* buf, size_t cap) {
  if (buf == nullptr || cap == 0) return 0;
  size_t pos = 0;
  buf[0] = '\0';
  for (uint8_t i = 0; i < TYPE_COUNT; i++) {
    int n = snprintf_P(buf + pos, cap - pos, PSTR("%s%u=%s"),
                     pos ? " " : "", (unsigned)TYPES[i].id, TYPES[i].name);
    if (n <= 0) break;
    if ((size_t)n >= cap - pos) { buf[pos] = '\0'; break; }
    pos += (size_t)n;
  }
  return pos;
}

size_t format_line(uint8_t type, int cps, int cpm, char* buf, size_t cap) {
  if (buf == nullptr || cap == 0) return 0;
  switch (type) {
    case GEIGER_STYPE_MIGHTYOHM: {
      char usv_str[12];
      format_f(usv_str, sizeof(usv_str), cpm / MIGHTYOHM_USV_DIV);
      int n = snprintf_P(buf, cap, PSTR("CPS, %d, CPM, %d, uSv/hr, %s, SLOW\n"),
                       cps, cpm, usv_str);
      return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
    }
    case GEIGER_STYPE_ESPGEIGER: {
      int n = snprintf_P(buf, cap, PSTR("CPM: %d\n"), cpm);
      return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
    }
    default: {
      // GC10 / GC10Next - plain integer.
      int n = snprintf_P(buf, cap, PSTR("%d\r\n"), cpm);
      return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
    }
  }
}

bool parse_cpm(uint8_t type, const char* input, int* out_cpm, int* out_cps) {
  if (input == nullptr || out_cpm == nullptr) return false;
  size_t len = strlen(input);
  if (len == 0) return false;

  for (size_t i = 0; i < len; i++) {
    char c = input[i];
    if (!isPrintable(c) && c != '\r' && c != '\n') return false;
  }

  int cpm = 0;
  int n = 0;
  switch (type) {
    case GEIGER_STYPE_MIGHTYOHM: {
      // INST mode (>255 cps): CPS field is CPS*60, so skip the fast-path.
      int cps;
      n = sscanf(input, "CPS, %d, CPM, %d", &cps, &cpm);
      if (n != 2) return false;
      if (out_cps && cps >= 0 && strstr(input, "INST") == nullptr) *out_cps = cps;
      break;
    }
    case GEIGER_STYPE_ESPGEIGER:
      n = sscanf(input, "CPM: %d", &cpm);
      if (n != 1) return false;
      break;
    default:
      // GC10 / GC10Next - digits-only (plus CR/LF) so garbage that
      // happens to start with a digit is rejected instead of parsed.
      for (size_t i = 0; i < len; i++) {
        if (!isDigit(input[i]) && input[i] != '\r' && input[i] != '\n') {
          return false;
        }
      }
      n = sscanf(input, "%d", &cpm);
      if (n != 1) return false;
      break;
  }

  // 1M CPM is orders of magnitude beyond any realistic tube.
  if (cpm < 0 || cpm > 1000000) return false;

  *out_cpm = cpm;
  return true;
}

}
