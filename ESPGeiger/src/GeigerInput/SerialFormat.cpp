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
#include "GeigerInput.h"   // GEIGER_IS_SERIAL macro
#include "../Util/StringUtil.h"
#include "../Util/OutputVars.h"
#include "../Counter/Counter.h"
#include "../Prefs/EGPrefs.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

extern Counter gcounter;

#define EG_HAS_SERIAL_PARSERS GEIGER_IS_SERIAL(GEIGER_TYPE)

namespace SerialFormat {

// MightyOhm wire constant: device's published uSv/hr = CPM / 175.
static constexpr float MIGHTYOHM_USV_DIV = 175.0f;

static size_t fmt_mightyohm(char* buf, size_t cap) {
  int cps = gcounter.get_last_cps();
  int cpm = gcounter.get_cpm();
  const char* mode;
  int reported_cpm = cpm;
  if (cps > 255)      { mode = "INST"; reported_cpm = cps * 60; }
  else if (cps > 15)  { mode = "FAST"; }
  else                { mode = "SLOW"; }
  char usv_str[12];
  format_f(usv_str, sizeof(usv_str), reported_cpm * (1.0f / MIGHTYOHM_USV_DIV));
  int n = snprintf_P(buf, cap, PSTR("CPS, %d, CPM, %d, uSv/hr, %s, %s\n"),
                     cps, reported_cpm, usv_str, mode);
  return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

static size_t fmt_user_template(char* buf, size_t cap) {
  const char* tpl = EGPrefs::getString("sout", "tpl");
  if (!tpl || !*tpl) return 0;
  size_t n = OutputVars::renderTemplate(tpl, buf, cap);
  if (n > 0 && buf[n - 1] != '\n' && n < cap - 1) {
    buf[n++] = '\n';
    buf[n]   = '\0';
  }
  return n;
}

#if EG_HAS_SERIAL_PARSERS

static bool common_validate(const char* in, size_t len) {
  if (len == 0) return false;
  for (size_t i = 0; i < len; i++) {
    char c = in[i];
    if (!isPrintable(c) && c != '\r' && c != '\n') return false;
  }
  return true;
}

static bool parse_mightyohm(const char* in, int* out_cpm, int* out_cps) {
  int cpm = 0, cps = 0;
  int n = sscanf(in, "CPS, %d, CPM, %d", &cps, &cpm);
  if (n != 2) return false;
  if (cpm < 0 || cpm > 1000000) return false;
  *out_cpm = cpm;
  if (out_cps && cps >= 0) *out_cps = cps;
  return true;
}

static bool parse_espg(const char* in, int* out_cpm, int* /*out_cps*/) {
  int cpm = 0;
  int n = sscanf(in, "CPM: %d", &cpm);
  if (n != 1 || cpm < 0 || cpm > 1000000) return false;
  *out_cpm = cpm;
  return true;
}

static bool parse_gc10(const char* in, int* out_cpm, int* /*out_cps*/) {
  // Digits-only (plus CR/LF). Rejects garbage that happens to lead with a
  // digit.
  for (size_t i = 0; in[i]; i++) {
    char c = in[i];
    if (!isDigit(c) && c != '\r' && c != '\n') return false;
  }
  int cpm = 0;
  int n = sscanf(in, "%d", &cpm);
  if (n != 1 || cpm < 0 || cpm > 1000000) return false;
  *out_cpm = cpm;
  return true;
}

#define IF_PARSE(p) p
#else
#define IF_PARSE(p) nullptr
#endif

// Per-protocol metadata. Templates are SRAM literals - tiny strings used
// only when a custom FormatFn isn't supplied. The template engine handles
// the {cpm} substitution path.
struct TypeInfo {
  uint8_t     id;
  uint32_t    baud;
  const char* name;
  const char* output_tpl;   // SRAM. Used when formatter==nullptr.
  FormatFn    formatter;    // Wins over output_tpl when non-null.
  ParseFn     parser;       // nullptr means input parse not supported.
  bool        has_cps;
};

static const TypeInfo TYPES[] = {
  { GEIGER_STYPE_ESPGEIGER, 115200, "ESPGeiger", "CPM: {cpm}\n", nullptr,            IF_PARSE(parse_espg),      false },
  { GEIGER_STYPE_TEMPLATE,  115200, "Template",  nullptr,        fmt_user_template,  nullptr,                   false },
#if EG_HAS_SERIAL_PARSERS
  { GEIGER_STYPE_GC10,      9600,   "GC10",      "{cpm}\r\n",    nullptr,            IF_PARSE(parse_gc10),      false },
  { GEIGER_STYPE_GC10NX,    115200, "GC10Next",  "{cpm}\r\n",    nullptr,            IF_PARSE(parse_gc10),      false },
#endif
  { GEIGER_STYPE_MIGHTYOHM, 9600,   "MightyOhm", nullptr,        fmt_mightyohm,      IF_PARSE(parse_mightyohm), true  },
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

static const char* g_resolve_tpl = nullptr;
static size_t fmt_render_resolved_tpl(char* buf, size_t cap) {
  if (!g_resolve_tpl) return 0;
  return OutputVars::renderTemplate(g_resolve_tpl, buf, cap);
}

FormatFn resolve(uint8_t type) {
  const TypeInfo* t = find(type);
  if (!t) return nullptr;
  if (t->formatter) return t->formatter;
  if (t->output_tpl) {
    g_resolve_tpl = t->output_tpl;
    return fmt_render_resolved_tpl;
  }
  return nullptr;
}

size_t format_line(uint8_t type, char* buf, size_t cap) {
  if (buf == nullptr || cap == 0) return 0;
  const TypeInfo* t = find(type);
  if (!t) return 0;
  if (t->formatter) return t->formatter(buf, cap);
  if (t->output_tpl) return OutputVars::renderTemplate(t->output_tpl, buf, cap);
  return 0;
}

bool parse_cpm(uint8_t type, const char* input, int* out_cpm, int* out_cps) {
#if EG_HAS_SERIAL_PARSERS
  if (!input || !out_cpm) return false;
  size_t len = strlen(input);
  if (!common_validate(input, len)) return false;
  const TypeInfo* t = find(type);
  if (!t || !t->parser) return false;
  return t->parser(input, out_cpm, out_cps);
#else
  (void)type; (void)input; (void)out_cpm; (void)out_cps;
  return false;
#endif
}

}
