/*
  corium::Measure.h - One table of measurements behind every output.

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

#ifndef CORIUM_MEASURE_H
#define CORIUM_MEASURE_H

#include <Arduino.h>
#include "App/AppConfig.h"

#ifndef CORIUM_M_ID_MAX
#define CORIUM_M_ID_MAX   9
#endif
#ifndef CORIUM_M_JSON_MAX
#define CORIUM_M_JSON_MAX 3   // /json key
#endif

class EGJson;

namespace corium {

enum MKind : uint8_t {
  M_F32,   // decimals 1..9
  M_U32,
  M_I32,
  M_BOOL,
  M_STR,   // SRAM or PROGMEM
};

enum MUnit : uint8_t {
  MU_NONE,
  MU_SEC,
  MU_BYTES,
  MU_DBM,
  MU_VOLT,
  MU_CELSIUS,
  MU_PCT,
  MU_HPA,
  MU_APP,   // app units from here
};

static constexpr uint8_t M_OUT_JSON    = 1 << 0;
static constexpr uint8_t M_OUT_PROM    = 1 << 1;  // /metrics
static constexpr uint8_t M_OUT_APP0    = 1 << 2;  // app sinks, the app names them
static constexpr uint8_t M_OUT_APP1    = 1 << 3;
static constexpr uint8_t M_OUT_VARS    = 1 << 4;  // templates and /vars.json
static constexpr uint8_t M_OUT_ID      = 1 << 5;  // by id only
static constexpr uint8_t M_OUT_ALL     = M_OUT_JSON | M_OUT_PROM | M_OUT_APP0 |
                                           M_OUT_APP1 | M_OUT_VARS;
static constexpr uint8_t M_OUT_ANY     = 0xFF;  // lookups by id

static constexpr uint8_t M_DIAG    = 1 << 0;  // only when diag asked for
static constexpr uint8_t M_COUNTER = 1 << 1;  // else gauge
static constexpr uint8_t M_NODIAG  = 1 << 2;  // left out when diag asked for

union MValue {
  float       f;
  uint32_t    u;
  int32_t     i;
  bool        b;
  const char* s;
};

// false = absent
typedef bool (*MRead)(MValue* out);

// tables must be PROGMEM
struct Measure {
  char        id[CORIUM_M_ID_MAX + 1];
  char        json[CORIUM_M_JSON_MAX + 1];   // "" = id
  const char* prom;         // PROGMEM, nullptr = not exported
  const char* promLabels;  // PROGMEM
  const char* help;         // PROGMEM
  MRead     read;
  MKind     kind;
  uint8_t     decimals;
  uint8_t     unit;         // MU_*
  uint8_t     outputs;      // M_OUT_*
  uint8_t     flags;        // M_DIAG, M_COUNTER
};

#define CORIUM_M_ROW(id, json, read, kind, dec, unit, out, flags) \
  {id, json, nullptr, nullptr, nullptr, read, kind, dec, unit, out, flags}
// on /metrics too
#define CORIUM_M_PROW(id, json, read, kind, dec, unit, out, flags, metric, help, labels) \
  {id, json, metric, labels, help, read, kind, dec, unit, (out) | corium::M_OUT_PROM, flags}

// SRAM copy, safe to pass anywhere
class MeasureRows {
public:
  constexpr MeasureRows() : _rows(nullptr), _n(0) {}
  template <size_t N>
  constexpr MeasureRows(const Measure (&rows)[N]) : _rows(rows), _n(N) {}
  size_t size() const { return _n; }
  Measure row(size_t j) const {
    Measure r;
    memcpy_P(&r, &_rows[j], sizeof(r));
    return r;
  }
  // id is PROGMEM, copies the id only
  bool idIs(size_t j, const char* id) const {
    char buf[sizeof(_rows[0].id)];
    memcpy_P(buf, _rows[j].id, sizeof(buf));
    return strcmp_P(buf, id) == 0;
  }
private:
  const Measure* _rows;
  size_t           _n;
};

typedef MeasureRows (*MSource)();

// walked core, input, then the modules. input nullptr = none
void m_sources(MeasureRows core, MSource input);

// rows for one output with a value now, id = that row only
class MeasureIter {
public:
  // id is PROGMEM
  explicit MeasureIter(uint8_t output, bool diag = false, const char* id = nullptr);
  bool next();
  const Measure& row() const   { return _row; }
  const MValue&  value() const { return _val; }
private:
  bool next_source();
  MeasureRows _src;
  size_t        _j;
  uint8_t       _mod;
  uint8_t       _out;
  bool          _diag;
  bool          _input;
  const char*   _id;
  Measure     _row;
  MValue      _val;
};

// 0 = truncated or unprintable, cut set if truncated
size_t m_format(const Measure& m, const MValue& v, char* buf, size_t cap, bool* cut = nullptr);

// 0 = no metric or truncated
size_t m_prom_format(const Measure& m, const MValue& v, const char* chipid,
                       bool head, char* buf, size_t cap);

inline const char* m_json_name(const Measure& m) {
  return m.json[0] ? m.json : m.id;
}

// celsius as stored
void m_json_fields(uint8_t output, EGJson& j);

// /json: short names, temperature in the display unit
void m_json_display(EGJson& j, bool diag);

// id PROGMEM, celsius. 0 and "" when absent
size_t m_text(const char* id, uint8_t output, char* buf, size_t cap);

// id PROGMEM. NAN, false or 0 when absent
float    m_f(const char* id);
bool     m_b(const char* id);
uint32_t m_u(const char* id);
int32_t  m_i(const char* id);

// id PROGMEM, core and input rows only, safe to cache
MRead m_core_read(const char* id, MKind kind);
// its value now, NAN / false when not found or gone
inline float m_read_f(MRead r) { MValue v; return (r && r(&v)) ? v.f : NAN; }
inline bool  m_read_b(MRead r) { MValue v; return r && r(&v) && v.b; }

// an app's unit pref maps straight on
enum MTempUnit : uint8_t { MT_C = 0, MT_F = 1, MT_K = 2 };

// rows store celsius
float m_celsius_to(float c, uint8_t unit);

uint8_t m_temp_unit();
void    m_set_temp_unit(uint8_t unit);

// unit MT_*, only celsius rows change
size_t m_format_unit(const Measure& m, const MValue& v, uint8_t unit,
                       char* buf, size_t cap, bool* cut = nullptr);

inline size_t m_format_display(const Measure& m, const MValue& v,
                                 char* buf, size_t cap) {
  return m_format_unit(m, v, m_temp_unit(), buf, cap);
}

}  // namespace corium

#endif
