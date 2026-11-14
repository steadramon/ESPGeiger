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
#include "Corium/Measure/Measure.h"

// sibling modules' rows; the counter's and the device's are read direct
// PSTR so the linker keeps one copy of each
#define M_HV     PSTR("hv")
// by id only
#define M_HV_TGT PSTR("hv_tgt")
#define M_HV_DTY PSTR("hv_duty")
#define M_HV_TRM PSTR("hv_trim")
#define M_RX_T   PSTR("rx_t")   // UdpRx /env
#define M_RX_H   PSTR("rx_h")
#define M_RX_P   PSTR("rx_p")
// built with the hv rows, 0 = the code reading them drops out
#ifdef ESPG_HV_ADC
#define M_HAS_HV 1
#else
#define M_HAS_HV 0
#endif
// tube volts, false with no hv row
inline bool m_hv_volts(int32_t* v) {
  corium::MeasureIter it(corium::M_OUT_ANY, false, M_HV);
  if (!it.next()) return false;
  *v = it.value().i;
  return true;
}
// celsius
#define M_T      PSTR("t")
#define M_H      PSTR("h")
#define M_P      PSTR("p")

#endif
