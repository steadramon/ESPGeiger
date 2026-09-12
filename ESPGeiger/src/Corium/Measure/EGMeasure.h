/*
  EGMeasure.h - One table of measurements behind every output.

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

#ifndef EGMEASURE_H
#define EGMEASURE_H

#include <Arduino.h>

enum EGMKind : uint8_t {
  EGM_F32,   // decimals 1..9
  EGM_U32,
  EGM_I32,
  EGM_BOOL,
  EGM_STR,   // SRAM or PROGMEM
};

enum EGMUnit : uint8_t {
  EGMU_NONE,
  EGMU_CPM,
  EGMU_CPS,
  EGMU_USVH,
  EGMU_COUNT,
  EGMU_SEC,
  EGMU_BYTES,
  EGMU_DBM,
  EGMU_VOLT,
  EGMU_CELSIUS,
  EGMU_PCT,
  EGMU_HPA,
};

static constexpr uint8_t EGM_OUT_JSON    = 1 << 0;
static constexpr uint8_t EGM_OUT_PROM    = 1 << 1;  // /metrics
static constexpr uint8_t EGM_OUT_WEBHOOK = 1 << 2;
static constexpr uint8_t EGM_OUT_MQTT    = 1 << 3;
static constexpr uint8_t EGM_OUT_VARS    = 1 << 4;  // templates and /vars.json
static constexpr uint8_t EGM_OUT_ID      = 1 << 5;  // by id only
static constexpr uint8_t EGM_OUT_ALL     = EGM_OUT_JSON | EGM_OUT_PROM | EGM_OUT_WEBHOOK |
                                           EGM_OUT_MQTT | EGM_OUT_VARS;

static constexpr uint8_t EGM_DIAG    = 1 << 0;
static constexpr uint8_t EGM_COUNTER = 1 << 1;  // else gauge
static constexpr uint8_t EGM_NODIAG  = 1 << 2;

union EGMValue {
  float       f;
  uint32_t    u;
  int32_t     i;
  bool        b;
  const char* s;
};

// false = absent
typedef bool (*EGMRead)(EGMValue* out);

// tables must be PROGMEM
struct EGMeasure {
  char        id[10];
  char        json[4];      // "" = id
  const char* prom;         // PROGMEM, nullptr = not exported
  const char* prom_labels;  // PROGMEM
  const char* help;         // PROGMEM
  EGMRead     read;
  EGMKind     kind;
  uint8_t     decimals;
  EGMUnit     unit;
  uint8_t     outputs;      // EGM_OUT_*
  uint8_t     flags;        // EGM_DIAG, EGM_COUNTER
};

#define EGM_ROW(id, json, read, kind, dec, unit, out, flags) \
  {id, json, nullptr, nullptr, nullptr, read, kind, dec, unit, out, flags}
// on /metrics too
#define EGM_PROW(id, json, read, kind, dec, unit, out, flags, metric, help, labels) \
  {id, json, metric, labels, help, read, kind, dec, unit, (out) | EGM_OUT_PROM, flags}

// SRAM copy, safe to pass anywhere
class EGMeasureRows {
public:
  constexpr EGMeasureRows() : _rows(nullptr), _n(0) {}
  template <size_t N>
  constexpr EGMeasureRows(const EGMeasure (&rows)[N]) : _rows(rows), _n(N) {}
  size_t size() const { return _n; }
  EGMeasure row(size_t j) const {
    EGMeasure r;
    memcpy_P(&r, &_rows[j], sizeof(r));
    return r;
  }
private:
  const EGMeasure* _rows;
  size_t           _n;
};

EGMeasureRows egm_core_rows();
EGMeasureRows egm_input_rows();

class EGMeasureIter {
public:
  // id is PROGMEM
  explicit EGMeasureIter(uint8_t output, bool diag = false, const char* id = nullptr);
  bool next();
  const EGMeasure& row() const   { return _row; }
  const EGMValue&  value() const { return _val; }
private:
  bool next_source();
  EGMeasureRows _src;
  size_t        _j;
  uint8_t       _mod;
  uint8_t       _out;
  bool          _diag;
  bool          _input;
  const char*   _id;
  EGMeasure     _row;
  EGMValue      _val;
};

// 0 = truncated or unprintable
size_t egm_format(const EGMeasure& m, const EGMValue& v, char* buf, size_t cap);

// 0 = no metric or truncated
size_t egm_prom_format(const EGMeasure& m, const EGMValue& v, const char* chipid,
                       bool head, char* buf, size_t cap);

inline const char* egm_json_name(const EGMeasure& m) {
  return m.json[0] ? m.json : m.id;
}

// celsius as stored
class EGJson;
void egm_json_fields(uint8_t output, EGJson& j);

// /json: short names, temperature in the display unit
void egm_json_display(EGJson& j, bool diag);

// id PROGMEM, celsius. 0 and "" when absent
size_t egm_text(const char* id, uint8_t output, char* buf, size_t cap);

// id PROGMEM. NAN, false or 0 when absent
float    egm_f(const char* id);
bool     egm_b(const char* id);
uint32_t egm_u(const char* id);

// id PROGMEM, core and input rows only, safe to cache
EGMRead egm_core_read(const char* id, EGMKind kind);

// oldest first, one a second
size_t egm_history(int* out, size_t cap);

// env.unit order
enum EGMTempUnit : uint8_t { EGMT_C = 0, EGMT_F = 1, EGMT_K = 2 };

// rows store celsius
float egm_celsius_to(float c, uint8_t unit);

uint8_t egm_temp_unit();
void    egm_set_temp_unit(uint8_t unit);

size_t egm_format_unit(const EGMeasure& m, const EGMValue& v, uint8_t unit,
                       char* buf, size_t cap);

inline size_t egm_format_display(const EGMeasure& m, const EGMValue& v,
                                 char* buf, size_t cap) {
  return egm_format_unit(m, v, egm_temp_unit(), buf, cap);
}

#endif
