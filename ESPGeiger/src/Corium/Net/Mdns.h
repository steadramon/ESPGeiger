/*
  Mdns.h - the core's MDNS, and a service moved or taken off on either stack

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
#ifndef CORIUM_MDNS_H
#define CORIUM_MDNS_H

#include <stdint.h>
#ifdef ESP8266
#include <ESP8266mDNS.h>
#else
#include <ESPmDNS.h>
#endif

// String literals only, idf wants "_osc" where MDNS takes "osc"
#define CORIUM_MDNS_MOVE(s, p, port) corium::Mdns::move(s, p, "_" s, "_" p, port)
#define CORIUM_MDNS_OFF(s, p)        corium::Mdns::off(s, p, "_" s, "_" p)

namespace corium {
namespace Mdns {

  // Both stacks refuse a second add, so a new port goes on the service that's already up
  static inline bool move(const char* s, const char* p, const char* us, const char* up, uint16_t port) {
#ifdef ESP8266
    (void)us; (void)up;
    (void)MDNS.removeService(nullptr, s, p);
#else
    // idf's remove is queued, an add straight after it is still refused
    if (mdns_service_port_set(us, up, port) == ESP_OK) return true;
#endif
    return MDNS.addService(s, p, port);
  }

  // First add only, use move() for a new port
  static inline bool add(const char* s, const char* p, uint16_t port) {
    return MDNS.addService(s, p, port);
  }

  static inline void txt(const char* s, const char* p, const char* key, const char* value) {
    MDNS.addServiceTxt(s, p, key, value);
  }

  static inline bool off(const char* s, const char* p, const char* us, const char* up) {
#ifdef ESP8266
    (void)us; (void)up;
    return MDNS.removeService(nullptr, s, p);
#else
    (void)s; (void)p;
    esp_err_t e = mdns_service_remove(us, up);
    return e == ESP_OK || e == ESP_ERR_NOT_FOUND;   // not there = off
#endif
  }

}
}  // namespace corium

#endif
