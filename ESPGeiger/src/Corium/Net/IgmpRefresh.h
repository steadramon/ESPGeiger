/*
  IgmpRefresh.h - fresh IGMP reports, group joins and the two cores' WiFiUDP quirks

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

bool refresh();   // call from the loop, only sends on esp32. false = not sent
// The 8266 answers queries itself, so its refresh() sends nothing
#ifdef ESP8266
constexpr bool REFRESHES = false;
#else
constexpr bool REFRESHES = true;
#endif
// Used on the 8266 only, its beginMulticast keeps the join even when the bind fails
CORIUM_NODISCARD bool join(const IPAddress& group);
void leave(const IPAddress& group);   // needed since WiFiUDP::stop() doesn't leave the group

// Call end() after the socket's stop()
struct Join {
#ifdef ESP8266
  IPAddress group;   // copy of the joined group, the caller's may change before end()

  template <class Udp>
  CORIUM_NODISCARD bool begin(Udp& udp, const IPAddress& g, uint16_t port) {
    if (!join(g)) return false;
    group = g;   // kept even if the bind fails, so end() still leaves it
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

// A packet to a group goes out on the station interface, the 8266 needs that named
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

// Drops a received packet. The esp32 keeps one until flush(), on the 8266 flush() sends instead
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
