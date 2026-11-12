/*
  IgmpRefresh.h - Fresh IGMP reports, group joins and the two cores' WiFiUDP quirks.

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
#ifndef CORIUM_IGMP_REFRESH_H
#define CORIUM_IGMP_REFRESH_H

#include <stdint.h>
#include "Corium/Util/Attr.h"
#ifdef ESP8266
#include "Corium/Net/Wifi.h"
#endif

class IPAddress;

namespace corium {
namespace IgmpRefresh {

bool refresh();   // esp32 only, loop, false = not sent
// esp8266 only: its beginMulticast keeps the join when the bind fails, so join here, one leave each
CORIUM_NODISCARD bool join(const IPAddress& group);
void leave(const IPAddress& group);   // its WiFiUDP::stop() keeps the group

// Udp = WiFiUDP
// a group listen, end() after the socket's stop()
struct Join {
#ifdef ESP8266
  IPAddress group;   // the joined one, the caller's can move first

  template <class Udp>
  CORIUM_NODISCARD bool begin(Udp& udp, const IPAddress& g, uint16_t port) {
    if (!join(g)) return false;
    group = g;   // a failed bind leaves it
    return udp.begin(port);
  }
  void end() {
    if (group.isSet()) leave(group);
    group.clear();
  }
#else
  template <class Udp>
  CORIUM_NODISCARD bool begin(Udp& udp, const IPAddress& g, uint16_t port) {
    return udp.beginMulticast(g, port);
  }
  void end() {}
#endif
};

// a packet to a group goes out the station, the 8266 needs it named
template <class Udp>
CORIUM_NODISCARD inline bool beginPacket(Udp& udp, const IPAddress& to, uint16_t port) {
#ifdef ESP8266
  return Wifi::isMulticast(to)
             ? udp.beginPacketMulticast(to, port, Wifi::local_ip)
             : udp.beginPacket(to, port);
#else
  return udp.beginPacket(to, port);
#endif
}

// a received packet dropped: esp32 keeps one till flush, the 8266's flush sends
template <class Udp>
inline void drop(Udp& udp) {
#ifdef ESP8266
  (void)udp;
#else
  udp.flush();
#endif
}

}
}  // namespace corium

#endif
