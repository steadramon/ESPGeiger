/*
  DeviceMeasures.cpp - The device rows, for the app's table.

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

#include "DeviceMeasures.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/TickProfile.h"
#include "Corium/Net/Wifi.h"

bool corium::m_dev_ut(corium::MValue* v)     { v->u = (uint32_t)corium::DeviceInfo::uptime(); return true; }
bool corium::m_dev_id(corium::MValue* v)     { v->s = corium::DeviceInfo::chipId(); return true; }
bool corium::m_dev_rssi(corium::MValue* v)   { v->i = (int32_t)corium::Wifi::rssi; return true; }
bool corium::m_dev_mem(corium::MValue* v)    { v->u = (uint32_t)corium::DeviceInfo::freeHeap(); return true; }
bool corium::m_dev_mem_raw(corium::MValue* v) { v->u = corium::DeviceInfo::heapNow(); return true; }
bool corium::m_dev_tick(corium::MValue* v)   { v->u = corium::TickProfile::tickUs; return true; }
bool corium::m_dev_t_max(corium::MValue* v)  { v->u = corium::TickProfile::tickMaxUs; return true; }
bool corium::m_dev_lps(corium::MValue* v)    { v->u = corium::TickProfile::lpsRaw; return true; }
bool corium::m_dev_frag(corium::MValue* v)   { v->u = corium::DeviceInfo::heapFrag(); return true; }
bool corium::m_dev_lfb(corium::MValue* v)    { v->u = corium::DeviceInfo::largestFreeBlock(); return true; }
bool corium::m_dev_lfblow(corium::MValue* v) { v->u = corium::DeviceInfo::largestFreeBlockLow(); return true; }

bool corium::m_dev_name(corium::MValue* v) {
  v->s = corium::DeviceInfo::displayName();
  return true;
}

const char corium::M_MP_UT[]   PROGMEM = "device_uptime_seconds";
const char corium::M_MH_UT[]   PROGMEM = "Seconds since boot";
const char corium::M_MP_MEM[]  PROGMEM = "device_free_heap_bytes";
const char corium::M_MH_MEM[]  PROGMEM = "Free heap memory";
const char corium::M_MP_RSSI[] PROGMEM = "device_wifi_rssi_dbm";
const char corium::M_MH_RSSI[] PROGMEM = "WiFi signal strength";
