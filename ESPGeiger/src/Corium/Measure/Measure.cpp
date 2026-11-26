/*
  Measure.cpp - one table of measurements behind every output

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

#include "Measure.h"
#include <EGFormat.h>
#include <EGJson.h>
#include <EGStr.h>
#include "Corium/Module/Module.h"
#include "Corium/Module/Registry.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/Util/Words.h"

// Most ids differ at the first byte, so this is quick
bool corium::MeasureRows::idIs(size_t j, const char* id) const {
  return streq_PP(_rows[j].id, id);
}

static corium::MeasureRows s_core;
static corium::MSource s_input = nullptr;
static bool s_rows_held = false;

void corium::m_sources(corium::MeasureRows core, corium::MSource input) {
  s_core = core;
  s_input = input;
}

void corium::m_hold(bool on) { s_rows_held = on; }

static corium::MeasureRows rows_of(corium::MSource f) {
  return f ? f() : corium::MeasureRows();
}

corium::MeasureIter::MeasureIter(uint8_t output, bool diag, const char* id)
  : _src(s_core), _j(0), _mod(0), _out(output), _diag(diag), _input(false),
    _id(id), _row(), _val() {}

// Core first, then input, then each module that isn't quarantined
bool corium::MeasureIter::next_source() {
  if (!_input) {
    _input = true;
    _src = rows_of(s_input);
    _j = 0;
    return true;
  }
  uint8_t n = corium::Registry::count();
  while (_mod < n) {
    corium::Module* m = corium::Registry::get(_mod++);
    if (!m || corium::Registry::quarantined(m)) continue;
    _src = m->measurements();
    _j = 0;
    return true;
  }
  return false;
}

static bool flags_skip(uint8_t f, bool diag) {
  return ((f & corium::M_DIAG) && !diag) || ((f & corium::M_NODIAG) && diag) ||
         ((f & corium::M_HELD) && s_rows_held);
}

// Skips rows that are absent, NaN or inf
bool corium::MeasureIter::next() {
  for (;;) {
    if (_j >= _src.size()) {
      if (!next_source()) return false;
      continue;
    }
    if (_id && !_src.idIs(_j, _id)) { _j++; continue; }
    _row = _src.row(_j++);
    if (!(_row.outputs & _out)) continue;
    if (flags_skip(_row.flags, _diag)) continue;
    if (!_row.read || !_row.read(&_val)) continue;
    if (_row.kind == corium::M_F32 && !isFinite(_val.f)) continue;
    return true;
  }
}

static uint8_t s_temp_unit = corium::MT_C;

uint8_t corium::m_temp_unit() { return s_temp_unit; }
void corium::m_set_temp_unit(uint8_t unit) { s_temp_unit = unit; }

float corium::m_celsius_to(float c, uint8_t unit) {
  if (unit == corium::MT_F) return c * 1.8f + 32.0f;
  if (unit == corium::MT_K) return c + 273.15f;
  return c;
}

size_t corium::m_format_unit(const corium::Measure& m, const corium::MValue& v, uint8_t unit,
                       char* buf, size_t cap, bool* cut) {
  corium::MValue out = v;
  if (m.unit == corium::MU_CELSIUS) out.f = corium::m_celsius_to(v.f, unit);
  return corium::m_format(m, out, buf, cap, cut);
}

// prints nothing if it would be truncated
size_t corium::m_format(const corium::Measure& m, const corium::MValue& v, char* buf, size_t cap, bool* cut) {
  int n;
  switch (m.kind) {
    case corium::M_F32:
      if (m.decimals < 1 || m.decimals > 9 || !isFinite(v.f)) return 0;
      n = format_f(buf, cap, v.f, m.decimals);
      break;
    case corium::M_U32:
      n = snprintf_P(buf, cap, W_FMT_LU, (unsigned long)v.u);
      break;
    case corium::M_I32:
      n = snprintf_P(buf, cap, W_FMT_LD, (long)v.i);
      break;
    case corium::M_BOOL:
      n = snprintf_P(buf, cap, W_FMT_U, v.b ? 1u : 0u);
      break;
    case corium::M_STR:
      if (!v.s) return 0;
      n = snprintf_P(buf, cap, W_FMT_S, v.s);
      break;
    default:
      return 0;
  }
  if (n < 0) return 0;
  if ((size_t)n >= cap) {
    if (cut) *cut = true;
    return 0;
  }
  return (size_t)n;
}

static void json_field(EGJson& j, const char* key, const corium::Measure& m, const corium::MValue& v) {
  switch (m.kind) {
    case corium::M_F32:  j.fixed(key, v.f, m.decimals); break;
    case corium::M_U32:  j.uint(key, v.u); break;
    case corium::M_I32:  j.sint(key, v.i); break;
    case corium::M_BOOL: j.uint(key, v.b ? 1 : 0); break;
    case corium::M_STR:  if (v.s) j.str(key, v.s); break;
    default: break;
  }
}

void corium::m_json_fields(uint8_t output, EGJson& j, bool diag) {
  for (corium::MeasureIter it(output, diag); it.next(); ) json_field(j, it.row().id, it.row(), it.value());
}

void corium::m_json_display(EGJson& j, bool diag) {
  for (corium::MeasureIter it(corium::M_OUT_JSON, diag); it.next(); ) {
    const corium::Measure& m = it.row();
    corium::MValue v = it.value();
    if (m.unit == corium::MU_CELSIUS) v.f = corium::m_celsius_to(v.f, corium::m_temp_unit());
    json_field(j, corium::m_json_name(m), m, v);
  }
}

size_t corium::m_text(const char* id, uint8_t output, char* buf, size_t cap) {
  size_t n = 0;
  corium::MeasureIter it(output, false, id);
  if (it.next()) n = corium::m_format(it.row(), it.value(), buf, cap);
  if (n == 0 && cap) buf[0] = '\0';
  return n;
}

static bool get(const char* id, corium::MKind kind, corium::MValue* out) {
  for (corium::MeasureIter it(corium::M_OUT_ANY, false, id); it.next(); ) {
    if (it.row().kind != kind) continue;
    *out = it.value();
    return true;
  }
  return false;
}

float corium::m_f(const char* id) {
  corium::MValue v;
  return get(id, corium::M_F32, &v) ? v.f : NAN;
}

bool corium::m_b(const char* id) {
  corium::MValue v;
  return get(id, corium::M_BOOL, &v) && v.b;
}

uint32_t corium::m_u(const char* id) {
  corium::MValue v;
  return get(id, corium::M_U32, &v) ? v.u : 0;
}

int32_t corium::m_i(const char* id) {
  corium::MValue v;
  return get(id, corium::M_I32, &v) ? v.i : 0;
}

corium::MRead corium::m_core_read(const char* id, corium::MKind kind) {
  const corium::MeasureRows srcs[] = {s_core, rows_of(s_input)};
  for (const corium::MeasureRows& src : srcs) {
    for (size_t j = 0; j < src.size(); j++) {
      if (!src.idIs(j, id)) continue;
      corium::MRow r = src.raw(j);
      if (r.kind == kind) return r.read;
    }
  }
  return nullptr;
}

static const char* or_blank(const char* s) { return s ? s : PSTR(""); }

static bool fits(int n, size_t cap) { return n >= 0 && (size_t)n < cap; }

static int prom_head(const corium::Measure& m, char* buf, size_t cap) {
  return snprintf_P(buf, cap, PSTR("# HELP %s %s\n# TYPE %s %s\n"),
                    m.prom, or_blank(m.help), m.prom,
                    (m.flags & corium::M_COUNTER) ? PSTR("counter") : PSTR("gauge"));
}

// If it doesn't fit the whole row is left out, head included
size_t corium::m_prom_format(const corium::Measure& m, const corium::MValue& v, const char* chipid,
                       bool head, char* buf, size_t cap) {
  if (!m.prom || m.kind == corium::M_STR) return 0;
  char text[24];
  if (corium::m_format(m, v, text, sizeof(text)) == 0) return 0;
  size_t n = 0;
  if (head) {
    int h = prom_head(m, buf, cap);
    if (!fits(h, cap)) return 0;
    n = (size_t)h;
  }
  int s = snprintf_P(buf + n, cap - n, PSTR("%s{chipid=\"%s\"%s} %s\n"),
                     m.prom, chipid, or_blank(m.promLabels), text);
  if (!fits(s, cap - n)) return 0;
  return n + (size_t)s;
}
