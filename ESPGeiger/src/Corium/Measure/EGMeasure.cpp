/*
  EGMeasure.cpp - One table of measurements behind every output.

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

#include "EGMeasure.h"
#include <EGFormat.h>
#include <EGJson.h>
#include "../Module/EGModule.h"
#include "../Module/EGModuleRegistry.h"
#include "../Util/StringUtil.h"

EGMeasureIter::EGMeasureIter(uint8_t output, bool diag, const char* id)
  : _src(egm_core_rows()), _j(0), _mod(0), _out(output), _diag(diag), _input(false),
    _id(id), _row(), _val() {}

bool EGMeasureIter::next_source() {
  if (!_input) {
    _input = true;
    _src = egm_input_rows();
    _j = 0;
    return true;
  }
  uint8_t n = EGModuleRegistry::count();
  while (_mod < n) {
    EGModule* m = EGModuleRegistry::get(_mod++);
    if (!m || EGModuleRegistry::quarantined(m)) continue;
    _src = m->measurements();
    _j = 0;
    return true;
  }
  return false;
}

// Skips other outputs, diag rows, absent and NaN
bool EGMeasureIter::next() {
  for (;;) {
    if (_j >= _src.size()) {
      if (!next_source()) return false;
      continue;
    }
    _row = _src.row(_j++);
    if (!(_row.outputs & _out)) continue;
    if ((_row.flags & EGM_DIAG) && !_diag) continue;
    if ((_row.flags & EGM_NODIAG) && _diag) continue;
    if (_id && strcmp_P(_row.id, _id) != 0) continue;
    if (!_row.read || !_row.read(&_val)) continue;
    if (_row.kind == EGM_F32 && isnan(_val.f)) continue;
    return true;
  }
}

static uint8_t s_temp_unit = EGMT_C;

uint8_t egm_temp_unit() { return s_temp_unit; }
void egm_set_temp_unit(uint8_t unit) { s_temp_unit = unit; }

float egm_celsius_to(float c, uint8_t unit) {
  if (unit == EGMT_F) return c * 1.8f + 32.0f;
  if (unit == EGMT_K) return c + 273.15f;
  return c;
}

size_t egm_format_unit(const EGMeasure& m, const EGMValue& v, uint8_t unit,
                       char* buf, size_t cap) {
  EGMValue out = v;
  if (m.unit == EGMU_CELSIUS) out.f = egm_celsius_to(v.f, unit);
  return egm_format(m, out, buf, cap);
}

// truncated prints nothing
size_t egm_format(const EGMeasure& m, const EGMValue& v, char* buf, size_t cap) {
  int n;
  switch (m.kind) {
    case EGM_F32:
      if (m.decimals < 1 || m.decimals > 9 || isnan(v.f)) return 0;
      n = format_f(buf, cap, v.f, m.decimals);
      break;
    case EGM_U32:
      n = snprintf_P(buf, cap, PSTR("%lu"), (unsigned long)v.u);
      break;
    case EGM_I32:
      n = snprintf_P(buf, cap, PSTR("%ld"), (long)v.i);
      break;
    case EGM_BOOL:
      n = snprintf_P(buf, cap, PSTR("%u"), v.b ? 1u : 0u);
      break;
    case EGM_STR:
      if (!v.s) return 0;
      n = snprintf_P(buf, cap, PSTR("%s"), v.s);
      break;
    default:
      return 0;
  }
  return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

static void json_field(EGJson& j, const char* key, const EGMeasure& m, const EGMValue& v) {
  switch (m.kind) {
    case EGM_F32:  j.fixed(key, v.f, m.decimals); break;
    case EGM_U32:  j.uint(key, v.u); break;
    case EGM_I32:  j.sint(key, v.i); break;
    case EGM_BOOL: j.uint(key, v.b ? 1 : 0); break;
    case EGM_STR:  if (v.s) j.str(key, v.s); break;
    default: break;
  }
}

void egm_json_fields(uint8_t output, EGJson& j) {
  for (EGMeasureIter it(output); it.next(); ) json_field(j, it.row().id, it.row(), it.value());
}

void egm_json_display(EGJson& j, bool diag) {
  for (EGMeasureIter it(EGM_OUT_JSON, diag); it.next(); ) {
    const EGMeasure& m = it.row();
    EGMValue v = it.value();
    if (m.unit == EGMU_CELSIUS) v.f = egm_celsius_to(v.f, egm_temp_unit());
    json_field(j, egm_json_name(m), m, v);
  }
}

size_t egm_text(const char* id, uint8_t output, char* buf, size_t cap) {
  size_t n = 0;
  EGMeasureIter it(output, false, id);
  if (it.next()) n = egm_format(it.row(), it.value(), buf, cap);
  if (n == 0 && cap) buf[0] = '\0';
  return n;
}

static bool get(const char* id, EGMKind kind, EGMValue* out) {
  for (EGMeasureIter it(0xFF, false, id); it.next(); ) {
    if (it.row().kind != kind) continue;
    *out = it.value();
    return true;
  }
  return false;
}

float egm_f(const char* id) {
  EGMValue v;
  return get(id, EGM_F32, &v) ? v.f : NAN;
}

bool egm_b(const char* id) {
  EGMValue v;
  return get(id, EGM_BOOL, &v) && v.b;
}

uint32_t egm_u(const char* id) {
  EGMValue v;
  return get(id, EGM_U32, &v) ? v.u : 0;
}

EGMRead egm_core_read(const char* id, EGMKind kind) {
  const EGMeasureRows srcs[] = {egm_core_rows(), egm_input_rows()};
  for (const EGMeasureRows& src : srcs) {
    for (size_t j = 0; j < src.size(); j++) {
      EGMeasure r = src.row(j);
      if (r.kind == kind && strcmp_P(r.id, id) == 0) return r.read;
    }
  }
  return nullptr;
}

static const char* or_blank(const char* s) { return s ? s : PSTR(""); }

static bool fits(int n, size_t cap) { return n >= 0 && (size_t)n < cap; }

static int prom_head(const EGMeasure& m, char* buf, size_t cap) {
  return snprintf_P(buf, cap, PSTR("# HELP %s %s\n# TYPE %s %s\n"),
                    m.prom, or_blank(m.help), m.prom,
                    (m.flags & EGM_COUNTER) ? PSTR("counter") : PSTR("gauge"));
}

// too big = left out whole, head too
size_t egm_prom_format(const EGMeasure& m, const EGMValue& v, const char* chipid,
                       bool head, char* buf, size_t cap) {
  if (!m.prom || m.kind == EGM_STR) return 0;
  char text[24];
  if (egm_format(m, v, text, sizeof(text)) == 0) return 0;
  size_t n = 0;
  if (head) {
    int h = prom_head(m, buf, cap);
    if (!fits(h, cap)) return 0;
    n = (size_t)h;
  }
  int s = snprintf_P(buf + n, cap - n, PSTR("%s{chipid=\"%s\"%s} %s\n"),
                     m.prom, chipid, or_blank(m.prom_labels), text);
  if (!fits(s, cap - n)) return 0;
  return n + (size_t)s;
}
