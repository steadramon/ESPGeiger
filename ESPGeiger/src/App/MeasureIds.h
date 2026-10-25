/*
  MeasureIds.h - Measurement ids the outputs read by name.

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
#ifndef APP_MEASURE_IDS_H
#define APP_MEASURE_IDS_H

#include "Corium/Platform/Flash.h"

// PSTR so the linker keeps one copy of each
#define M_ALERT  PSTR("alert")
#define M_WARN   PSTR("warn")
#define M_WARM   PSTR("warm")
#define M_CPM    PSTR("cpm")
#define M_CPM5   PSTR("cpm5")
#define M_CPM15  PSTR("cpm15")
#define M_CPM_ST PSTR("cpm_st")
#define M_CPS    PSTR("cps")
#define M_CPS_I  PSTR("cps_i")
#define M_USV    PSTR("usv")
#define M_USV5   PSTR("usv5")
#define M_USV_ST PSTR("usv_st")
#define M_TC     PSTR("tc")
#define M_SER_OK PSTR("ser_ok")
#define M_HV     PSTR("hv")
// celsius
#define M_T      PSTR("t")
#define M_H      PSTR("h")
#define M_P      PSTR("p")

#endif
