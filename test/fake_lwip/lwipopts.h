/*
  lwipopts.h - lwIP options for native unit tests.

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

// match firmware: pio run -e espgeigerhw -t idedata

#ifndef FAKE_LWIPOPTS_H
#define FAKE_LWIPOPTS_H

// Device build flags
#ifndef TCP_MSS
#define TCP_MSS 536
#endif
#define LWIP_IPV6      0
#define LWIP_FEATURES  1

#define TCP_SND_BUF    (4 * TCP_MSS)

#define LWIP_TCP       1
#define LWIP_DNS       1
#define LWIP_CALLBACK_API 1

#define NO_SYS         1
#define LWIP_NETCONN   0
#define LWIP_SOCKET    0

// needs pbuf code the fake lacks
#define LWIP_ICMP      0
#define LWIP_IGMP      0
#define LWIP_DHCP      0
#define LWIP_AUTOIP    0
#define LWIP_UDP       0
#define LWIP_RAW       0
#define LWIP_STATS     0

#endif
