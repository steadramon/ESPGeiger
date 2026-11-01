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

#include "Corium/Measure/Measure.h"

namespace corium {

bool m_dev_ut(MValue* v);
bool m_dev_id(MValue* v);
bool m_dev_name(MValue* v);
bool m_dev_rssi(MValue* v);
bool m_dev_mem(MValue* v);
bool m_dev_mem_raw(MValue* v);
bool m_dev_tick(MValue* v);
bool m_dev_t_max(MValue* v);
bool m_dev_lps(MValue* v);
bool m_dev_frag(MValue* v);
bool m_dev_lfb(MValue* v);
bool m_dev_lfblow(MValue* v);

extern const char M_MP_UT[];
extern const char M_MH_UT[];
extern const char M_MP_MEM[];
extern const char M_MH_MEM[];
extern const char M_MP_RSSI[];
extern const char M_MH_RSSI[];

// rows for the app's table, the app picks the outputs
#define CORIUM_M_ROW_UPTIME(out) \
  CORIUM_M_PROW("ut", "", corium::m_dev_ut, corium::M_U32, 0, corium::MU_SEC, out, 0, corium::M_MP_UT, corium::M_MH_UT, nullptr)
// not on diag, CORIUM_M_ROW_MEM_RAW is
#define CORIUM_M_ROW_MEM(out) \
  CORIUM_M_PROW("mem", "", corium::m_dev_mem, corium::M_U32, 0, corium::MU_BYTES, out, corium::M_NODIAG, corium::M_MP_MEM, corium::M_MH_MEM, nullptr)
#define CORIUM_M_ROW_MEM_RAW(out) \
  CORIUM_M_ROW("mem", "", corium::m_dev_mem_raw, corium::M_U32, 0, corium::MU_BYTES, out, corium::M_DIAG)
#define CORIUM_M_ROW_RSSI(out) \
  CORIUM_M_PROW("rssi", "", corium::m_dev_rssi, corium::M_I32, 0, corium::MU_DBM, out, 0, corium::M_MP_RSSI, corium::M_MH_RSSI, nullptr)
#define CORIUM_M_ROW_ID(out)     CORIUM_M_ROW("id", "", corium::m_dev_id, corium::M_STR, 0, corium::MU_NONE, out, 0)
#define CORIUM_M_ROW_NAME(out)   CORIUM_M_ROW("name", "", corium::m_dev_name, corium::M_STR, 0, corium::MU_NONE, out, 0)
// diag only
#define CORIUM_M_ROW_TICK(out)   CORIUM_M_ROW("tick", "", corium::m_dev_tick, corium::M_U32, 0, corium::MU_NONE, out, corium::M_DIAG)
#define CORIUM_M_ROW_T_MAX(out)  CORIUM_M_ROW("t_max", "", corium::m_dev_t_max, corium::M_U32, 0, corium::MU_NONE, out, corium::M_DIAG)
#define CORIUM_M_ROW_LPS(out)    CORIUM_M_ROW("lps", "", corium::m_dev_lps, corium::M_U32, 0, corium::MU_NONE, out, corium::M_DIAG)
#define CORIUM_M_ROW_FRAG(out)   CORIUM_M_ROW("frag", "", corium::m_dev_frag, corium::M_U32, 0, corium::MU_PCT, out, corium::M_DIAG)
#define CORIUM_M_ROW_LFB(out)    CORIUM_M_ROW("lfb", "", corium::m_dev_lfb, corium::M_U32, 0, corium::MU_BYTES, out, corium::M_DIAG)
#define CORIUM_M_ROW_LFBLOW(out) CORIUM_M_ROW("lfblow", "", corium::m_dev_lfblow, corium::M_U32, 0, corium::MU_BYTES, out, corium::M_DIAG)

}  // namespace corium

#endif
