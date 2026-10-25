/*
  WifiLink.cpp - WiFi link state and reconnect tracking.

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
#include "WifiLink.h"
#include "Wifi.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include "Corium/Logger/Logger.h"
#include "Corium/System/Fs.h"
#include "Corium/System/Lifecycle.h"

const char WIFI_BACKUP_P[] PROGMEM = "/wifi_backup";
const char NET_BACKUP_P[] PROGMEM = "/net_backup";

namespace Wifi {
  bool connected = false;
  IPAddress local_ip;
  char ssid[WIFI_SSID_MAX + 1] = "";
  int16_t rssi = 0;
  uint32_t connected_at_ms = 0;
  bool settled = false;
  bool backup_grace_done = false;

  void check_backup_state() {
    backup_grace_done = !Fs::exists(WIFI_BACKUP_PATH)
                     && !Fs::exists(NET_BACKUP_PATH);
  }
}

static bool was_connected = false;
static unsigned long lost_at = 0;
static uint32_t s_retry_at = 0;
static unsigned long s_link_at = 0;
static unsigned long s_offline_retry_at = 0;
static const uint32_t WIFI_RETRY_MS = 30000UL;
static const uint32_t WIFI_LINK_MS = 60000UL;   // rssi + ip
static const uint32_t WIFI_BACKUP_MS = 300000UL;
static const uint32_t WIFI_OFFLINE_RETRY_MS = 120000UL;

static void refresh_link_cache(bool just_connected, unsigned long now) {
  if (!just_connected && !reached(now, s_link_at)) return;
  s_link_at = now + WIFI_LINK_MS;
  Wifi::rssi = WiFi.RSSI();
  Wifi::local_ip = WiFi.localIP();
  if (!just_connected) {
    if ((int32_t)(now - Wifi::connected_at_ms) < 0) Wifi::settled = true;
    return;
  }
  strlcpy(Wifi::ssid, WiFi.SSID().c_str(), sizeof(Wifi::ssid));
  Wifi::connected_at_ms = now ? now : now - 1;   // 0 = down
  Wifi::settled = false;
}

static void track_outage(unsigned long now) {
  if (was_connected) {
    was_connected = false;
    lost_at = now;
    s_retry_at = now + WIFI_RETRY_MS;
    Wifi::connected_at_ms = 0;
    Log::console(PSTR("WiFi: Connection lost"));
  } else if (lost_at == 0) {
    lost_at = now;
    s_retry_at = now + WIFI_RETRY_MS;
  }
  unsigned long down_seconds = (now - lost_at) / 1000;
  if (due_every(now, s_retry_at, WIFI_RETRY_MS)) {
    Log::console(PSTR("WiFi: Attempting reconnect (%lus)"), down_seconds);
    WiFi.reconnect();
  }
  if (down_seconds > WIFI_DOWN_MAX_S) {
    Log::console(PSTR("WiFi: Down for %lus, rebooting"), down_seconds);
    Lifecycle::request_restart(now, 100);
  }
}

static void note_reconnected(unsigned long now) {
  was_connected = true;
  if (lost_at > 0) {
    unsigned long down_seconds = (now - lost_at) / 1000;
    Log::console(PSTR("WiFi: Reconnected after %lus"), down_seconds);
  }
}

static void reprobe_saved_network(unsigned long now) {
  if (Wifi::connected) {
    Log::console(PSTR("WiFi: Saved network back, rebooting to reconnect"));
    Lifecycle::request_restart(now, 500);
  } else if (now > WIFI_OFFLINE_MAX_MS) {
    Log::console(PSTR("WiFi: Offline %lus, rebooting to retry"), now / 1000);
    Lifecycle::request_restart(now, 500);
  } else if (s_offline_retry_at == 0) {
    s_offline_retry_at = now + WIFI_OFFLINE_RETRY_MS;
  } else if (reached(now, s_offline_retry_at)) {
    s_offline_retry_at = now + WIFI_OFFLINE_RETRY_MS;
    Log::console(PSTR("WiFi: Offline - re-probing saved network"));
    WiFi.mode(WIFI_STA);
    WiFi.begin();
  }
}

static void clear_backups_once_stable() {
  Wifi::backup_grace_done = true;
  if (Fs::remove(WIFI_BACKUP_PATH)) {
    Log::console(PSTR("WiFi: New creds stable for 5 min, backup cleared"));
  }
  if (Fs::remove(NET_BACKUP_PATH)) {
    Log::console(PSTR("WiFi: Static IP stable for 5 min, net backup cleared"));
  }
}

bool Wifi::look(unsigned long now) {
  wl_status_t wifi_status = WiFi.status();
  bool prev = Wifi::connected;
  Wifi::connected = (wifi_status == WL_CONNECTED);
  if (Wifi::connected) refresh_link_cache(!prev, now);

  if (!Wifi::disabled) {
    if (wifi_status != WL_CONNECTED) track_outage(now);
    else if (!was_connected) note_reconnected(now);
  } else if (!Wifi::offline_forced) {
    reprobe_saved_network(now);
  }

  if (!Wifi::backup_grace_done && Wifi::connected && now > WIFI_BACKUP_MS) clear_backups_once_stable();
  return Wifi::connected != prev;
}
