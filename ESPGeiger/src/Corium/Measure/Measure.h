/*
  Measure.h - one table of measurements behind every output

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
  MU_APP,   // App units from here
};

static constexpr uint8_t M_OUT_JSON    = 1 << 0;
static constexpr uint8_t M_OUT_PROM    = 1 << 1;  // /metrics
static constexpr uint8_t M_OUT_APP0    = 1 << 2;  // App sinks, the app names them
static constexpr uint8_t M_OUT_APP1    = 1 << 3;
static constexpr uint8_t M_OUT_VARS    = 1 << 4;  // Templates and /vars.json
static constexpr uint8_t M_OUT_ID      = 1 << 5;  // by id only
static constexpr uint8_t M_OUT_PS      = 1 << 6;  // /ps, diag rows too
static constexpr uint8_t M_OUT_ALL     = M_OUT_JSON | M_OUT_PROM | M_OUT_APP0 |
                                         M_OUT_APP1 | M_OUT_VARS;
static constexpr uint8_t M_OUT_ANY     = 0xFF;  // lookups by id

static constexpr uint8_t M_DIAG    = 1 << 0;  // Only when diag asked for
static constexpr uint8_t M_COUNTER = 1 << 1;  // counter, else a gauge
static constexpr uint8_t M_NODIAG  = 1 << 2;  // left out when diag asked for
static constexpr uint8_t M_HELD    = 1 << 3;  // Left out while m_hold is on

union MValue {
  float       f;
  uint32_t    u;
  int32_t     i;
  bool        b;
  const char* s;
};

// false = absent
typedef bool (*MRead)(MValue* out);

// /metrics name, PROGMEM
struct MProm {
  const char* metric;
  const char* labels;   // nullptr = none
  const char* help;
};

// Tables must be PROGMEM
struct MRow {
  char        id[CORIUM_M_ID_MAX + 1];
  char        json[CORIUM_M_JSON_MAX + 1];   // "" = id
  MKind       kind;         // Sits in the pad after json
  uint8_t     decimals;
  MRead       read;
  uint8_t     unit;         // MU_*
  uint8_t     outputs;      // M_OUT_*
  uint8_t     flags;        // M_DIAG, M_COUNTER
  uint8_t     promIdx;      // 0 = none, n = MProm[n - 1] in the rows' prom table
};
static_assert(sizeof(void*) != 4 || sizeof(MRow) == 24, "no pad between fields");

// A row as read out, the prom pointers point at PROGMEM
struct Measure : MRow {
  const char* prom;         // nullptr = not exported
  const char* promLabels;
  const char* help;
};

#define CORIUM_M_ROW(id, json, read, kind, dec, unit, out, flags) \
  {id, json, kind, dec, read, unit, out, flags, 0}
// Same but on /metrics too, prom = promIdx
#define CORIUM_M_PROW(id, json, read, kind, dec, unit, out, flags, prom) \
  {id, json, kind, dec, read, unit, (out) | corium::M_OUT_PROM, flags, prom}

// SRAM copy, safe to pass anywhere
class MeasureRows {
public:
  constexpr MeasureRows() : _rows(nullptr), _prom(nullptr), _n(0), _nprom(0) {}
  template <size_t N>
  constexpr MeasureRows(const MRow (&rows)[N])
    : _rows(rows), _prom(nullptr), _n(N), _nprom(0) {}
  template <size_t N, size_t P>
  constexpr MeasureRows(const MRow (&rows)[N], const MProm (&prom)[P])
    : _rows(rows), _prom(prom), _n(N), _nprom(P) {
    static_assert(P < 256, "promIdx is 8 bits");
  }
  size_t size() const { return _n; }
  // Row without the /metrics names
  MRow raw(size_t j) const {
    MRow r;
    memcpy_P(&r, &_rows[j], sizeof(r));
    return r;
  }
  Measure row(size_t j) const {
    Measure m;
    memcpy_P(static_cast<MRow*>(&m), &_rows[j], sizeof(MRow));
    MProm p = {nullptr, nullptr, nullptr};
    if (m.promIdx && m.promIdx <= _nprom) memcpy_P(&p, &_prom[m.promIdx - 1], sizeof(p));
    m.prom = p.metric;
    m.promLabels = p.labels;
    m.help = p.help;
    return m;
  }
  // id can be SRAM or PROGMEM, compared in place
  bool idIs(size_t j, const char* id) const;
private:
  const MRow*  _rows;
  const MProm* _prom;
  uint16_t     _n;
  uint16_t     _nprom;
};

typedef MeasureRows (*MSource)();

// Rows are walked core first, then input, then the modules. input nullptr = none
void m_sources(MeasureRows core, MSource input);
void m_hold(bool on);   // M_HELD rows are skipped, not read

// Walks the rows for one output that have a value now. With an id, only that row
class MeasureIter {
public:
  // id is PROGMEM
  explicit MeasureIter(uint8_t output, bool diag = false, const char* id = nullptr);
  bool next();
  const Measure& row() const   { return _row; }
  const MValue&  value() const { return _val; }
private:
  bool next_source();
  MeasureRows   _src;
  size_t        _j;
  uint8_t       _mod;
  uint8_t       _out;
  bool          _diag;
  bool          _input;
  const char*   _id;
  Measure       _row;
  MValue        _val;
};

// 0 = truncated or unprintable, cut set if truncated
size_t m_format(const Measure& m, const MValue& v, char* buf, size_t cap, bool* cut = nullptr);

// 0 = no metric or truncated
size_t m_prom_format(const Measure& m, const MValue& v, const char* chipid,
                     bool head, char* buf, size_t cap);

inline const char* m_json_name(const Measure& m) {
  return m.json[0] ? m.json : m.id;
}

// Temperatures stay in celsius as stored
void m_json_fields(uint8_t output, EGJson& j, bool diag = false);

// /json: short names, temperature in the display unit
void m_json_display(EGJson& j, bool diag);

// id is PROGMEM, temps in celsius. 0 and "" when absent
size_t m_text(const char* id, uint8_t output, char* buf, size_t cap);

// id PROGMEM. NAN, false or 0 when absent
float    m_f(const char* id);
bool     m_b(const char* id);
uint32_t m_u(const char* id);
int32_t  m_i(const char* id);

// id PROGMEM. Core and input rows only, so the result is safe to cache
MRead m_core_read(const char* id, MKind kind);
// Reads the value now, NAN / false when not found or gone
inline float m_read_f(MRead r) { MValue v; return (r && r(&v)) ? v.f : NAN; }
inline bool  m_read_b(MRead r) { MValue v; return r && r(&v) && v.b; }

// Same numbers as an app's unit pref, so it maps straight on
enum MTempUnit : uint8_t { MT_C = 0, MT_F = 1, MT_K = 2 };

// Rows store celsius
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
