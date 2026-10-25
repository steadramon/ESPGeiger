/*
  DeviceMeasures.h - The device rows, for the app's table.

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

#ifndef DEVICE_MEASURES_H
#define DEVICE_MEASURES_H

#include "Corium/Measure/EGMeasure.h"

bool egm_dev_ut(EGMValue* v);
bool egm_dev_id(EGMValue* v);
bool egm_dev_name(EGMValue* v);
bool egm_dev_rssi(EGMValue* v);
bool egm_dev_mem(EGMValue* v);
bool egm_dev_mem_raw(EGMValue* v);
bool egm_dev_tick(EGMValue* v);
bool egm_dev_t_max(EGMValue* v);
bool egm_dev_lps(EGMValue* v);
bool egm_dev_frag(EGMValue* v);
bool egm_dev_lfb(EGMValue* v);
bool egm_dev_lfblow(EGMValue* v);

extern const char EGM_MP_UT[];
extern const char EGM_MH_UT[];
extern const char EGM_MP_MEM[];
extern const char EGM_MH_MEM[];
extern const char EGM_MP_RSSI[];
extern const char EGM_MH_RSSI[];

// rows for the app's table, the app picks the outputs
#define EGM_ROW_UPTIME(out) \
  EGM_PROW("ut", "", egm_dev_ut, EGM_U32, 0, EGMU_SEC, out, 0, EGM_MP_UT, EGM_MH_UT, nullptr)
// not on diag, EGM_ROW_MEM_RAW is
#define EGM_ROW_MEM(out) \
  EGM_PROW("mem", "", egm_dev_mem, EGM_U32, 0, EGMU_BYTES, out, EGM_NODIAG, EGM_MP_MEM, EGM_MH_MEM, nullptr)
#define EGM_ROW_MEM_RAW(out) \
  EGM_ROW("mem", "", egm_dev_mem_raw, EGM_U32, 0, EGMU_BYTES, out, EGM_DIAG)
#define EGM_ROW_RSSI(out) \
  EGM_PROW("rssi", "", egm_dev_rssi, EGM_I32, 0, EGMU_DBM, out, 0, EGM_MP_RSSI, EGM_MH_RSSI, nullptr)
#define EGM_ROW_ID(out)     EGM_ROW("id", "", egm_dev_id, EGM_STR, 0, EGMU_NONE, out, 0)
#define EGM_ROW_NAME(out)   EGM_ROW("name", "", egm_dev_name, EGM_STR, 0, EGMU_NONE, out, 0)
// diag only
#define EGM_ROW_TICK(out)   EGM_ROW("tick", "", egm_dev_tick, EGM_U32, 0, EGMU_NONE, out, EGM_DIAG)
#define EGM_ROW_T_MAX(out)  EGM_ROW("t_max", "", egm_dev_t_max, EGM_U32, 0, EGMU_NONE, out, EGM_DIAG)
#define EGM_ROW_LPS(out)    EGM_ROW("lps", "", egm_dev_lps, EGM_U32, 0, EGMU_NONE, out, EGM_DIAG)
#define EGM_ROW_FRAG(out)   EGM_ROW("frag", "", egm_dev_frag, EGM_U32, 0, EGMU_PCT, out, EGM_DIAG)
#define EGM_ROW_LFB(out)    EGM_ROW("lfb", "", egm_dev_lfb, EGM_U32, 0, EGMU_BYTES, out, EGM_DIAG)
#define EGM_ROW_LFBLOW(out) EGM_ROW("lfblow", "", egm_dev_lfblow, EGM_U32, 0, EGMU_BYTES, out, EGM_DIAG)

#endif
