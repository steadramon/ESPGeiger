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

bool egm_dev_ut(EGMValue* v)     { v->u = (uint32_t)DeviceInfo::uptime(); return true; }
bool egm_dev_id(EGMValue* v)     { v->s = DeviceInfo::chipid(); return true; }
bool egm_dev_rssi(EGMValue* v)   { v->i = (int32_t)Wifi::rssi; return true; }
bool egm_dev_mem(EGMValue* v)    { v->u = (uint32_t)DeviceInfo::freeHeap(); return true; }
bool egm_dev_mem_raw(EGMValue* v) { v->u = DeviceInfo::heapNow(); return true; }
bool egm_dev_tick(EGMValue* v)   { v->u = TickProfile::tick_us; return true; }
bool egm_dev_t_max(EGMValue* v)  { v->u = TickProfile::tick_max_us; return true; }
bool egm_dev_lps(EGMValue* v)    { v->u = TickProfile::lps_raw; return true; }
bool egm_dev_frag(EGMValue* v)   { v->u = DeviceInfo::heapFrag(); return true; }
bool egm_dev_lfb(EGMValue* v)    { v->u = DeviceInfo::largestFreeBlock(); return true; }
bool egm_dev_lfblow(EGMValue* v) { v->u = DeviceInfo::largestFreeBlockLow(); return true; }

bool egm_dev_name(EGMValue* v) {
  const char* fn = DeviceInfo::friendlyName();
  v->s = (fn && fn[0]) ? fn : DeviceInfo::hostname();
  return true;
}

const char EGM_MP_UT[]   PROGMEM = "device_uptime_seconds";
const char EGM_MH_UT[]   PROGMEM = "Seconds since boot";
const char EGM_MP_MEM[]  PROGMEM = "device_free_heap_bytes";
const char EGM_MH_MEM[]  PROGMEM = "Free heap memory";
const char EGM_MP_RSSI[] PROGMEM = "device_wifi_rssi_dbm";
const char EGM_MH_RSSI[] PROGMEM = "WiFi signal strength";
