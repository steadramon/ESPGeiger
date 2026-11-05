/*
  CounterMeasures.cpp - The core measurement table.

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

#include "Corium/Measure/Measure.h"
#include "Corium/Measure/DeviceMeasures.h"
#include "App/Units.h"
#include "Counter.h"
#include "CounterMeasures.h"
#include "CounterUpload.h"
#include "GeigerInput/SerialFormat.h"
#include "Corium/Logger/Logger.h"

static bool m_cpmf(corium::MValue* v)  { v->f = gcounter.get_cpmf(); return true; }
static bool m_cpm_i(corium::MValue* v) { v->i = gcounter.get_cpm(); return true; }
static bool m_cps(corium::MValue* v)   { v->f = gcounter.get_cps(); return true; }
static bool m_cps_i(corium::MValue* v) { v->i = gcounter.get_last_cps(); return true; }
static bool m_cpm5(corium::MValue* v)  { v->f = gcounter.get_cpm5f(); return true; }
static bool m_cpm15(corium::MValue* v) { v->f = gcounter.get_cpm15f(); return true; }
static bool m_usv(corium::MValue* v)   { v->f = gcounter.get_usv(); return true; }
static bool m_ratio(corium::MValue* v) { v->f = gcounter.get_ratio(); return true; }
static bool m_tc(corium::MValue* v)    { v->u = (uint32_t)gcounter.total_clicks; return true; }
static bool m_tube(corium::MValue* v)  { v->b = gcounter.get_tube_alive(); return true; }
static bool m_sat(corium::MValue* v)   { v->b = gcounter.get_saturated(); return true; }
static bool m_warn(corium::MValue* v)  { v->b = gcounter.is_warning(); return true; }
static bool m_alert(corium::MValue* v) { v->b = gcounter.is_alert(); return true; }
static bool m_warm(corium::MValue* v)  { v->b = gcounter.is_warm(); return true; }
static bool m_none(corium::MValue* v)  { v->b = gcounter.counts_missing(); return true; }
static bool m_blip(corium::MValue* v)  { v->u = (uint32_t)gcounter.last_blip(); return true; }
static bool m_cpm_st(corium::MValue* v) { v->f = gcounter.get_cpmf_stable(); return true; }
static bool m_usv_st(corium::MValue* v) { v->f = gcounter.get_usv_stable(); return true; }
static bool m_usv5(corium::MValue* v)   { v->f = gcounter.get_usv5(); return true; }
static bool m_ser_ok(corium::MValue* v) { v->b = gcounter.is_healthy(); return true; }

static bool m_life(corium::MValue* v) {
  uint64_t n = gcounter.get_lifetime_clicks_total();
  v->u = n > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)n;
  return true;
}

static bool m_mode(corium::MValue* v) {
  v->s = SerialFormat::mohm_mode(gcounter.get_last_cps());
  return true;
}

static bool m_cs_live(corium::MValue* v) {
  if (gcounter.get_cps_n() < 2) return false;
  v->f = gcounter.get_cps_live();
  return true;
}
static bool m_cs_n(corium::MValue* v) {
  if (gcounter.get_cps_n() < 2) return false;
  v->u = gcounter.get_cps_n();
  return true;
}
static bool m_cs_win(corium::MValue* v) {
  if (gcounter.get_cps_n() < 2) return false;
  v->u = gcounter.get_cps_win_us();
  return true;
}

static const char MP_CPM[]   PROGMEM = "geiger_cpm";
static const char MH_CPM[]   PROGMEM = "Counts per minute (window label selects averaging period)";
static const char MP_USV[]   PROGMEM = "geiger_usv_per_hour";
static const char MH_USV[]   PROGMEM = "Dose rate in microsieverts per hour";
static const char MP_CPS[]   PROGMEM = "geiger_cps";
static const char MH_CPS[]   PROGMEM = "Instantaneous counts per second";
static const char MP_RATIO[] PROGMEM = "geiger_usv_ratio";
static const char MH_RATIO[] PROGMEM = "CPM per uSv/h conversion ratio";
static const char MP_LIFE[]  PROGMEM = "geiger_lifetime_clicks_total";
static const char MH_LIFE[]  PROGMEM = "Total clicks counted over device life";
static const char MP_TUBE[]  PROGMEM = "geiger_tube_alive";
static const char MH_TUBE[]  PROGMEM = "1 while counts arrive, 0 once silent longer than expected for the rate";
static const char MP_SAT[]   PROGMEM = "geiger_saturated";
static const char MH_SAT[]   PROGMEM = "1 when the count rate is near the dead-time limit";
static const char ML_1M[]    PROGMEM = ",window=\"1m\"";
static const char ML_5M[]    PROGMEM = ",window=\"5m\"";
static const char ML_15M[]   PROGMEM = ",window=\"15m\"";

static constexpr uint8_t JV   = corium::M_OUT_JSON | corium::M_OUT_VARS;
static constexpr uint8_t JVW  = JV | EGM_OUT_WEBHOOK;
static constexpr uint8_t JVWM = JVW | EGM_OUT_MQTT;
static constexpr uint8_t JWM  = corium::M_OUT_JSON | EGM_OUT_WEBHOOK | EGM_OUT_MQTT;

// keep a metric's rows together
static const corium::Measure CORE[] PROGMEM = {
  CORIUM_M_ROW_UPTIME(JVW),
  CORIUM_M_PROW("cpm",      "c",   m_cpmf,    corium::M_F32,  2, EGMU_CPM,     JWM,          0, MP_CPM, MH_CPM, ML_1M),
  CORIUM_M_ROW("cpm",       "",    m_cpm_i,   corium::M_I32,  0, EGMU_CPM,     corium::M_OUT_VARS, 0),
  CORIUM_M_PROW("cpm5",     "c5",  m_cpm5,    corium::M_F32,  2, EGMU_CPM,     JVWM,         0, MP_CPM, MH_CPM, ML_5M),
  CORIUM_M_PROW("cpm15",    "c15", m_cpm15,   corium::M_F32,  2, EGMU_CPM,     JVWM,         0, MP_CPM, MH_CPM, ML_15M),
  CORIUM_M_PROW("usv",      "s",   m_usv,     corium::M_F32,  4, EGMU_USVH,    JVWM,         0, MP_USV, MH_USV, nullptr),
  CORIUM_M_PROW("cps",      "cs",  m_cps,     corium::M_F32,  2, EGMU_CPS,     JVWM,         0, MP_CPS, MH_CPS, nullptr),
  CORIUM_M_ROW("cps_i",     "",    m_cps_i,   corium::M_I32,  0, EGMU_CPS,     corium::M_OUT_VARS, 0),
  CORIUM_M_PROW("ratio",    "r",   m_ratio,   corium::M_F32,  2, corium::MU_NONE,    corium::M_OUT_JSON, 0, MP_RATIO, MH_RATIO, nullptr),
  CORIUM_M_ROW("tc",        "",    m_tc,      corium::M_U32,  0, EGMU_COUNT,   JVW,          0),
  CORIUM_M_PROW("life",     "",    m_life,    corium::M_U32,  0, EGMU_COUNT,   corium::M_OUT_VARS, corium::M_COUNTER, MP_LIFE, MH_LIFE, nullptr),
  CORIUM_M_ROW_MEM(JVW),
  CORIUM_M_ROW_MEM_RAW(corium::M_OUT_JSON),
  CORIUM_M_ROW_RSSI(JVW),
  CORIUM_M_ROW_ID(corium::M_OUT_VARS),
  CORIUM_M_ROW_NAME(corium::M_OUT_VARS),
  CORIUM_M_ROW("mode",      "",    m_mode,    corium::M_STR,  0, corium::MU_NONE,    corium::M_OUT_VARS, 0),
  CORIUM_M_PROW("tube",     "",    m_tube,    corium::M_BOOL, 0, corium::MU_NONE,    JVWM,         0, MP_TUBE, MH_TUBE, nullptr),
  CORIUM_M_PROW("sat",      "",    m_sat,     corium::M_BOOL, 0, corium::MU_NONE,    JVWM,         0, MP_SAT, MH_SAT, nullptr),
  CORIUM_M_ROW("warn",      "",    m_warn,    corium::M_BOOL, 0, corium::MU_NONE,    JVWM,         0),
  CORIUM_M_ROW("alert",     "",    m_alert,   corium::M_BOOL, 0, corium::MU_NONE,    JVWM,         0),
  CORIUM_M_ROW_TICK(corium::M_OUT_JSON),
  CORIUM_M_ROW_T_MAX(corium::M_OUT_JSON),
  CORIUM_M_ROW_LPS(corium::M_OUT_JSON),
  CORIUM_M_ROW_FRAG(corium::M_OUT_JSON),
  CORIUM_M_ROW_LFB(corium::M_OUT_JSON),
  CORIUM_M_ROW_LFBLOW(corium::M_OUT_JSON),
  CORIUM_M_ROW("cs_live",   "",    m_cs_live, corium::M_F32,  2, EGMU_CPS,     corium::M_OUT_JSON, corium::M_DIAG),
  CORIUM_M_ROW("cs_n",      "",    m_cs_n,    corium::M_U32,  0, EGMU_COUNT,   corium::M_OUT_JSON, corium::M_DIAG),
  CORIUM_M_ROW("cs_win_us", "",    m_cs_win,  corium::M_U32,  0, corium::MU_NONE,    corium::M_OUT_JSON, corium::M_DIAG),
  CORIUM_M_ROW("warm",      "",    m_warm,    corium::M_BOOL, 0, corium::MU_NONE,    corium::M_OUT_ID,   0),
  CORIUM_M_ROW("no_counts", "",    m_none,    corium::M_BOOL, 0, corium::MU_NONE,    corium::M_OUT_ID,   0),
  CORIUM_M_ROW("blip",      "",    m_blip,    corium::M_U32,  0, corium::MU_NONE,    corium::M_OUT_ID,   0),
  // uploaders, 60 s bucket
  CORIUM_M_ROW("cpm_st",    "",    m_cpm_st,  corium::M_F32,  2, EGMU_CPM,     corium::M_OUT_ID,   0),
  CORIUM_M_ROW("usv_st",    "",    m_usv_st,  corium::M_F32,  4, EGMU_USVH,    corium::M_OUT_ID,   0),
  CORIUM_M_ROW("usv5",      "",    m_usv5,    corium::M_F32,  4, EGMU_USVH,    corium::M_OUT_ID,   0),
  CORIUM_M_ROW("ser_ok",    "",    m_ser_ok,  corium::M_BOOL, 0, corium::MU_NONE,    corium::M_OUT_ID,   0),
};

static corium::MeasureRows input_rows() {
  GeigerInput* in = gcounter.input();
  return in ? in->measurements() : corium::MeasureRows();
}

void measures_begin() { corium::m_sources(corium::MeasureRows(CORE), input_rows); }

size_t egm_history(int* out, size_t cap) {
  size_t n = gcounter.cpm_history.size();
  if (n > cap) n = cap;
  size_t skip = gcounter.cpm_history.size() - n;
  for (size_t i = 0; i < n; i++) out[i] = gcounter.cpm_history[skip + i];
  return n;
}

size_t egm_cpm5_history(uint16_t* out, size_t cap) {
  gcounter.want_cpm5_history();
  if (!gcounter.cpm5_history) return 0;
  CircularBuffer<uint16_t,45>& b = *gcounter.cpm5_history;
  size_t n = b.size();
  if (n > cap) n = cap;
  size_t skip = b.size() - n;
  for (size_t i = 0; i < n; i++) out[i] = b[skip + i];
  return n;
}

bool upload_warm() { return gcounter.is_warm(); }

void upload_testmode(const char* tag) {
  if (gcounter.is_warm()) corium::Log::console(PSTR("%s: Testmode"), tag);
}
