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
#include <EGFormat.h>
#include "SerialParse.h"
#include "GeigerInput.h"
#include "../Corium/Util/StringUtil.h"
#include "../Corium/Measure/OutputVars.h"
#include "../Corium/Util/Words.h"
#include "../Counter/Counter.h"
#include "../Corium/Prefs/EGPrefs.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define EG_HAS_SERIAL_PARSERS GEIGER_IS_SERIAL(GEIGER_TYPE)

namespace SerialFormat {

// MightyOhm uSv/hr = CPM / 175
static constexpr float MIGHTYOHM_USV_DIV = 175.0f;

const char* mohm_mode(int cps) {
  return (cps > 255) ? PSTR("INST") : (cps > 15) ? PSTR("FAST") : PSTR("SLOW");
}

size_t fmt_mightyohm(char* buf, size_t cap) {
  int cps = gcounter.get_last_cps();
  int reported_cpm = gcounter.get_cpm();
  // cps*60 overflows int above ~35.8M cps
  if (cps > 255) reported_cpm = (cps > 35791394) ? 2147483647 : cps * 60;
  char usv_str[12];
  format_f(usv_str, sizeof(usv_str), reported_cpm * (1.0f / MIGHTYOHM_USV_DIV));
  int n = snprintf_P(buf, cap, PSTR("CPS, %d, CPM, %d, uSv/hr, %s, %s\n"),
                     cps, reported_cpm, usv_str, mohm_mode(cps));
  return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

size_t fmt_user_template(char* buf, size_t cap) {
  const char* tpl = EGPrefs::getString("sout", EG_K_TPL);
  if (!tpl || !*tpl) return 0;
  size_t n = OutputVars::renderTemplate(tpl, buf, cap);
  if (n > 0 && buf[n - 1] != '\n' && n < cap - 1) {
    buf[n++] = '\n';
    buf[n]   = '\0';
  }
  return n;
}

#if EG_HAS_SERIAL_PARSERS
#define IF_PARSE(p) p
#else
#define IF_PARSE(p) nullptr
#endif

struct TypeInfo {
  uint8_t     id;
  uint32_t    baud;
  const char* name;         // flash
  const char* output_tpl;   // flash, if no formatter
  FormatFn    formatter;
  ParseFn     parser;       // nullptr = no input parse
  bool        has_cps;
};

#if EG_HAS_SERIAL_PARSERS
static const char N_GC10[]   PROGMEM = "GC10";
static const char N_GC10NX[] PROGMEM = "GC10Next";
static const char T_CRLF[]   PROGMEM = "{cpm}\r\n";
#endif
static const char N_MOHM[]   PROGMEM = "MightyOhm";
static const char N_ESPG[]   PROGMEM = "ESPGeiger";
static const char N_TPL[]    PROGMEM = "Template";
static const char T_ESPG[]   PROGMEM = "CPM: {cpm}\n";

static const TypeInfo TYPES[] PROGMEM = {
#if EG_HAS_SERIAL_PARSERS
  { GEIGER_STYPE_GC10,      9600,   N_GC10,   T_CRLF,  nullptr,            IF_PARSE(parse_gc10),      false },
  { GEIGER_STYPE_GC10NX,    115200, N_GC10NX, T_CRLF,  nullptr,            IF_PARSE(parse_gc10),      false },
#endif
  { GEIGER_STYPE_MIGHTYOHM, 9600,   N_MOHM,   nullptr, fmt_mightyohm,      IF_PARSE(parse_mightyohm), true  },
  { GEIGER_STYPE_ESPGEIGER, 115200, N_ESPG,   T_ESPG,  nullptr,            IF_PARSE(parse_espgeiger), false },
  { GEIGER_STYPE_TEMPLATE,  115200, N_TPL,    nullptr, fmt_user_template,  IF_PARSE(parse_template),  false },
};
static constexpr uint8_t TYPE_COUNT = sizeof(TYPES) / sizeof(TYPES[0]);

static bool find(uint8_t id, TypeInfo& t) {
  for (uint8_t i = 0; i < TYPE_COUNT; i++) {
    memcpy_P(&t, &TYPES[i], sizeof(t));
    if (t.id == id) return true;
  }
  return false;
}

uint32_t    baud_for(uint8_t type)  { TypeInfo t; return find(type, t) ? t.baud : 0; }
const char* name_for(uint8_t type)  { TypeInfo t; return find(type, t) ? t.name : nullptr; }
bool        is_known(uint8_t type)  { TypeInfo t; return find(type, t); }
bool        has_cps(uint8_t type)   { TypeInfo t; return find(type, t) ? t.has_cps : false; }

// TYPES order
const char TYPE_OPTS[] PROGMEM =
#if EG_HAS_SERIAL_PARSERS
  "GC10|GC10Next|"
#endif
  "MightyOhm|ESPGeiger|Template";

static size_t render_tpl_P(const char* tpl, char* buf, size_t cap) {
  char t[16];
  strncpy_P(t, tpl, sizeof(t) - 1);
  t[sizeof(t) - 1] = '\0';
  return OutputVars::renderTemplate(t, buf, cap);
}

size_t format_line(uint8_t type, char* buf, size_t cap) {
  if (buf == nullptr || cap == 0) return 0;
  TypeInfo t;
  if (!find(type, t)) return 0;
  if (t.formatter) return t.formatter(buf, cap);
  if (t.output_tpl) return render_tpl_P(t.output_tpl, buf, cap);
  return 0;
}

bool parse_cpm(uint8_t type, const char* input, int* out_cpm, int* out_cps) {
#if EG_HAS_SERIAL_PARSERS
  if (!input || !out_cpm) return false;
  size_t len = strlen(input);
  if (!common_validate(input, len)) return false;
  TypeInfo t;
  if (!find(type, t) || !t.parser) return false;
  return t.parser(input, out_cpm, out_cps);
#else
  (void)type; (void)input; (void)out_cpm; (void)out_cps;
  return false;
#endif
}

}
