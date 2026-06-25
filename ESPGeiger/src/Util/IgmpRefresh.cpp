/*
  IgmpRefresh.cpp - see header.

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
#include "IgmpRefresh.h"

#ifdef ESP32
#include "Wifi.h"
#include <tcpip_adapter.h>
#include <lwip/netif.h>
#include <lwip/igmp.h>
#include <lwip/tcpip.h>

namespace IgmpRefresh {

// Must run on the tcpip thread: igmp_report_groups mutates
// netif->igmp_group_list which the lwIP worker also touches.
static void report_groups_cb(void* ctx) {
  igmp_report_groups((struct netif*)ctx);
}

void refresh() {
  if (!Wifi::connected) return;
  void* nif = nullptr;
  if (tcpip_adapter_get_netif(TCPIP_ADAPTER_IF_STA, &nif) != ESP_OK || !nif) return;
  tcpip_callback(report_groups_cb, nif);
}

}

#else  // ESP8266 or other targets - bug doesn't manifest here.

namespace IgmpRefresh { void refresh() {} }

#endif
