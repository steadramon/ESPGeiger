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

#include "Corium/Measure/EGMeasure.h"
#include "Counter.h"
#include "GeigerInput/SerialFormat.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/TickProfile.h"
#include "Corium/Net/Wifi.h"

static bool m_cpmf(EGMValue* v)  { v->f = gcounter.get_cpmf(); return true; }
static bool m_cpm_i(EGMValue* v) { v->i = gcounter.get_cpm(); return true; }
static bool m_cps(EGMValue* v)   { v->f = gcounter.get_cps(); return true; }
static bool m_cps_i(EGMValue* v) { v->i = gcounter.get_last_cps(); return true; }
static bool m_cpm5(EGMValue* v)  { v->f = gcounter.get_cpm5f(); return true; }
static bool m_cpm15(EGMValue* v) { v->f = gcounter.get_cpm15f(); return true; }
static bool m_usv(EGMValue* v)   { v->f = gcounter.get_usv(); return true; }
static bool m_ratio(EGMValue* v) { v->f = gcounter.get_ratio(); return true; }
static bool m_tc(EGMValue* v)    { v->u = (uint32_t)gcounter.total_clicks; return true; }
static bool m_ut(EGMValue* v)    { v->u = (uint32_t)DeviceInfo::uptime(); return true; }
static bool m_id(EGMValue* v)    { v->s = DeviceInfo::chipid(); return true; }
static bool m_rssi(EGMValue* v)  { v->i = (int32_t)Wifi::rssi; return true; }
static bool m_mem(EGMValue* v)   { v->u = (uint32_t)DeviceInfo::freeHeap(); return true; }
static bool m_mem_raw(EGMValue* v) { v->u = (uint32_t)ESP.getFreeHeap(); return true; }
static bool m_tube(EGMValue* v)  { v->b = gcounter.get_tube_alive(); return true; }
static bool m_sat(EGMValue* v)   { v->b = gcounter.get_saturated(); return true; }
static bool m_warn(EGMValue* v)  { v->b = gcounter.is_warning(); return true; }
static bool m_alert(EGMValue* v) { v->b = gcounter.is_alert(); return true; }
static bool m_warm(EGMValue* v)  { v->b = gcounter.is_warm(); return true; }
static bool m_none(EGMValue* v)  { v->b = gcounter.counts_missing(); return true; }
static bool m_blip(EGMValue* v)  { v->u = (uint32_t)gcounter.last_blip(); return true; }
static bool m_cpm_st(EGMValue* v) { v->f = gcounter.get_cpmf_stable(); return true; }
static bool m_usv_st(EGMValue* v) { v->f = gcounter.get_usv_stable(); return true; }
static bool m_usv5(EGMValue* v)   { v->f = gcounter.get_usv5(); return true; }
static bool m_ser_ok(EGMValue* v) { v->b = gcounter.is_healthy(); return true; }

static bool m_tick(EGMValue* v)   { v->u = TickProfile::tick_us; return true; }
static bool m_t_max(EGMValue* v)  { v->u = TickProfile::tick_max_us; return true; }
static bool m_lps(EGMValue* v)    { v->u = TickProfile::lps_raw; return true; }
static bool m_frag(EGMValue* v)   { v->u = DeviceInfo::heapFrag(); return true; }
static bool m_lfb(EGMValue* v)    { v->u = DeviceInfo::largestFreeBlock(); return true; }
static bool m_lfblow(EGMValue* v) { v->u = DeviceInfo::largestFreeBlockLow(); return true; }

static bool m_life(EGMValue* v) {
  uint64_t n = gcounter.get_lifetime_clicks_total();
  v->u = n > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)n;
  return true;
}

static bool m_name(EGMValue* v) {
  const char* fn = DeviceInfo::friendlyName();
  v->s = (fn && fn[0]) ? fn : DeviceInfo::hostname();
  return true;
}

static bool m_mode(EGMValue* v) {
  v->s = SerialFormat::mohm_mode(gcounter.get_last_cps());
  return true;
}

static bool m_cs_live(EGMValue* v) {
  if (gcounter.get_cps_n() < 2) return false;
  v->f = gcounter.get_cps_live();
  return true;
}
static bool m_cs_n(EGMValue* v) {
  if (gcounter.get_cps_n() < 2) return false;
  v->u = gcounter.get_cps_n();
  return true;
}
static bool m_cs_win(EGMValue* v) {
  if (gcounter.get_cps_n() < 2) return false;
  v->u = gcounter.get_cps_win_us();
  return true;
}

static const char MP_UT[]    PROGMEM = "device_uptime_seconds";
static const char MH_UT[]    PROGMEM = "Seconds since boot";
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
static const char MP_MEM[]   PROGMEM = "device_free_heap_bytes";
static const char MH_MEM[]   PROGMEM = "Free heap memory";
static const char MP_RSSI[]  PROGMEM = "device_wifi_rssi_dbm";
static const char MH_RSSI[]  PROGMEM = "WiFi signal strength";
static const char MP_TUBE[]  PROGMEM = "geiger_tube_alive";
static const char MH_TUBE[]  PROGMEM = "1 while counts arrive, 0 once silent longer than expected for the rate";
static const char MP_SAT[]   PROGMEM = "geiger_saturated";
static const char MH_SAT[]   PROGMEM = "1 when the count rate is near the dead-time limit";
static const char ML_1M[]    PROGMEM = ",window=\"1m\"";
static const char ML_5M[]    PROGMEM = ",window=\"5m\"";
static const char ML_15M[]   PROGMEM = ",window=\"15m\"";

static constexpr uint8_t JV   = EGM_OUT_JSON | EGM_OUT_VARS;
static constexpr uint8_t JVW  = JV | EGM_OUT_WEBHOOK;
static constexpr uint8_t JVWM = JVW | EGM_OUT_MQTT;
static constexpr uint8_t JWM  = EGM_OUT_JSON | EGM_OUT_WEBHOOK | EGM_OUT_MQTT;

// keep a metric's rows together
static const EGMeasure CORE[] PROGMEM = {
  EGM_PROW("ut",       "",    m_ut,      EGM_U32,  0, EGMU_SEC,     JVW,          0, MP_UT, MH_UT, nullptr),
  EGM_PROW("cpm",      "c",   m_cpmf,    EGM_F32,  2, EGMU_CPM,     JWM,          0, MP_CPM, MH_CPM, ML_1M),
  EGM_ROW("cpm",       "",    m_cpm_i,   EGM_I32,  0, EGMU_CPM,     EGM_OUT_VARS, 0),
  EGM_PROW("cpm5",     "c5",  m_cpm5,    EGM_F32,  2, EGMU_CPM,     JVWM,         0, MP_CPM, MH_CPM, ML_5M),
  EGM_PROW("cpm15",    "c15", m_cpm15,   EGM_F32,  2, EGMU_CPM,     JVWM,         0, MP_CPM, MH_CPM, ML_15M),
  EGM_PROW("usv",      "s",   m_usv,     EGM_F32,  4, EGMU_USVH,    JVWM,         0, MP_USV, MH_USV, nullptr),
  EGM_PROW("cps",      "cs",  m_cps,     EGM_F32,  2, EGMU_CPS,     JVWM,         0, MP_CPS, MH_CPS, nullptr),
  EGM_ROW("cps_i",     "",    m_cps_i,   EGM_I32,  0, EGMU_CPS,     EGM_OUT_VARS, 0),
  EGM_PROW("ratio",    "r",   m_ratio,   EGM_F32,  2, EGMU_NONE,    EGM_OUT_JSON, 0, MP_RATIO, MH_RATIO, nullptr),
  EGM_ROW("tc",        "",    m_tc,      EGM_U32,  0, EGMU_COUNT,   JVW,          0),
  EGM_PROW("life",     "",    m_life,    EGM_U32,  0, EGMU_COUNT,   EGM_OUT_VARS, EGM_COUNTER, MP_LIFE, MH_LIFE, nullptr),
  EGM_PROW("mem",      "",    m_mem,     EGM_U32,  0, EGMU_BYTES,   JVW,          EGM_NODIAG, MP_MEM, MH_MEM, nullptr),
  EGM_ROW("mem",       "",    m_mem_raw, EGM_U32,  0, EGMU_BYTES,   EGM_OUT_JSON, EGM_DIAG),
  EGM_PROW("rssi",     "",    m_rssi,    EGM_I32,  0, EGMU_DBM,     JVW,          0, MP_RSSI, MH_RSSI, nullptr),
  EGM_ROW("id",        "",    m_id,      EGM_STR,  0, EGMU_NONE,    EGM_OUT_VARS, 0),
  EGM_ROW("name",      "",    m_name,    EGM_STR,  0, EGMU_NONE,    EGM_OUT_VARS, 0),
  EGM_ROW("mode",      "",    m_mode,    EGM_STR,  0, EGMU_NONE,    EGM_OUT_VARS, 0),
  EGM_PROW("tube",     "",    m_tube,    EGM_BOOL, 0, EGMU_NONE,    JVWM,         0, MP_TUBE, MH_TUBE, nullptr),
  EGM_PROW("sat",      "",    m_sat,     EGM_BOOL, 0, EGMU_NONE,    JVWM,         0, MP_SAT, MH_SAT, nullptr),
  EGM_ROW("warn",      "",    m_warn,    EGM_BOOL, 0, EGMU_NONE,    JVWM,         0),
  EGM_ROW("alert",     "",    m_alert,   EGM_BOOL, 0, EGMU_NONE,    JVWM,         0),
  EGM_ROW("tick",      "",    m_tick,    EGM_U32,  0, EGMU_NONE,    EGM_OUT_JSON, EGM_DIAG),
  EGM_ROW("t_max",     "",    m_t_max,   EGM_U32,  0, EGMU_NONE,    EGM_OUT_JSON, EGM_DIAG),
  EGM_ROW("lps",       "",    m_lps,     EGM_U32,  0, EGMU_NONE,    EGM_OUT_JSON, EGM_DIAG),
  EGM_ROW("frag",      "",    m_frag,    EGM_U32,  0, EGMU_PCT,     EGM_OUT_JSON, EGM_DIAG),
  EGM_ROW("lfb",       "",    m_lfb,     EGM_U32,  0, EGMU_BYTES,   EGM_OUT_JSON, EGM_DIAG),
  EGM_ROW("lfblow",    "",    m_lfblow,  EGM_U32,  0, EGMU_BYTES,   EGM_OUT_JSON, EGM_DIAG),
  EGM_ROW("cs_live",   "",    m_cs_live, EGM_F32,  2, EGMU_CPS,     EGM_OUT_JSON, EGM_DIAG),
  EGM_ROW("cs_n",      "",    m_cs_n,    EGM_U32,  0, EGMU_COUNT,   EGM_OUT_JSON, EGM_DIAG),
  EGM_ROW("cs_win_us", "",    m_cs_win,  EGM_U32,  0, EGMU_NONE,    EGM_OUT_JSON, EGM_DIAG),
  EGM_ROW("warm",      "",    m_warm,    EGM_BOOL, 0, EGMU_NONE,    EGM_OUT_ID,   0),
  EGM_ROW("no_counts", "",    m_none,    EGM_BOOL, 0, EGMU_NONE,    EGM_OUT_ID,   0),
  EGM_ROW("blip",      "",    m_blip,    EGM_U32,  0, EGMU_NONE,    EGM_OUT_ID,   0),
  // uploaders, 60 s bucket
  EGM_ROW("cpm_st",    "",    m_cpm_st,  EGM_F32,  2, EGMU_CPM,     EGM_OUT_ID,   0),
  EGM_ROW("usv_st",    "",    m_usv_st,  EGM_F32,  4, EGMU_USVH,    EGM_OUT_ID,   0),
  EGM_ROW("usv5",      "",    m_usv5,    EGM_F32,  4, EGMU_USVH,    EGM_OUT_ID,   0),
  EGM_ROW("ser_ok",    "",    m_ser_ok,  EGM_BOOL, 0, EGMU_NONE,    EGM_OUT_ID,   0),
};

EGMeasureRows egm_core_rows() { return EGMeasureRows(CORE); }

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

EGMeasureRows egm_input_rows() {
  GeigerInput* in = gcounter.input();
  return in ? in->measurements() : EGMeasureRows();
}
