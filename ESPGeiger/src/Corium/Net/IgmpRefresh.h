/*
  IgmpRefresh.h - Force a fresh on-wire IGMP report for joined groups.

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

#include "Corium/Util/Attr.h"

class IPAddress;

namespace corium {
namespace IgmpRefresh {

bool refresh();   // esp32 only, loop, false = not sent
// esp8266 only: its beginMulticast keeps the join when the bind fails, so join here, one leave each
CORIUM_NODISCARD bool join(const IPAddress& group);
void leave(const IPAddress& group);   // its WiFiUDP::stop() keeps the group

}
}  // namespace corium

#endif
