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
#include "Corium/System/FastMillis.h"
#include "Corium/System/Fs.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/System/BootGuard.h"

const char WIFI_BACKUP_P[] PROGMEM = "/wifi_backup";
const char NET_BACKUP_P[] PROGMEM = "/net_backup";

namespace corium {
namespace Wifi {
  bool connected = false;
  bool ap = false;
  IPAddress local_ip;
  char ssid[WIFI_SSID_MAX + 1] = {};
  int16_t rssi = 0;
  uint32_t connectedAtMs = 0;
  bool settled = false;
  bool backupGraceDone = false;

  void checkBackupState() {
    backupGraceDone = !Fs::exists(WIFI_BACKUP_PATH)
                     && !Fs::exists(NET_BACKUP_PATH);
  }
}
}  // namespace corium

static unsigned long s_lost_at = 0;   // 0 = never lost
static uint32_t s_retry_at = 0;
static unsigned long s_link_at = 0;
static unsigned long s_offline_retry_at = 0;
static const uint32_t WIFI_RETRY_MS = 30000UL;
static const uint32_t WIFI_LINK_MS = 60000UL;   // refresh rssi + ip
static const uint32_t WIFI_BACKUP_MS = 300000UL;   // Once up this long after boot, the backups are removed
static const uint32_t WIFI_OFFLINE_RETRY_MS = 120000UL;   // while disabled, try the saved network this often

// An automatic restart mustn't be a way out of safe mode
static void restart_soon(unsigned long now, uint32_t ms) {
  if (corium::BootGuard::safeMode()) corium::BootGuard::scram();
  corium::Lifecycle::requestRestart(now, ms);
}

static void refresh_link_cache(bool just_connected, unsigned long now) {
  if (!just_connected && !corium::reached(now, s_link_at)) return;
  s_link_at = now + WIFI_LINK_MS;
  corium::Wifi::rssi = WiFi.RSSI();
  corium::Wifi::local_ip = WiFi.localIP();
  if (!just_connected) {
    // Set settled before the u32 diff wraps round to small again
    uint32_t up = (uint32_t)(now - corium::Wifi::connectedAtMs);
    if (up > INT32_MAX) corium::Wifi::settled = true;
    return;
  }
  strlcpy(corium::Wifi::ssid, WiFi.SSID().c_str(), sizeof(corium::Wifi::ssid));
  corium::Wifi::connectedAtMs = now ? now : now - 1;   // 0 = down
  corium::Wifi::settled = false;
}

static void track_outage(unsigned long now) {
  if (corium::Wifi::state() != corium::Wifi::LINK_LOST) {
    // Joined at boot and the loop never saw it up, so no log line
    if (corium::Wifi::connectedAtMs) corium::Log::console(PSTR("WiFi: Connection lost"));
    s_lost_at = now;
    s_retry_at = now + WIFI_RETRY_MS;
    corium::Wifi::connectedAtMs = 0;
    corium::Wifi::go(corium::Wifi::LINK_LOST, PSTR("link down"));
  }
  unsigned long down_s = (now - s_lost_at) / 1000;
  if (corium::dueEvery(now, s_retry_at, WIFI_RETRY_MS)) {
    corium::Log::console(PSTR("WiFi: Attempting reconnect (%lus)"), down_s);
    WiFi.reconnect();
  }
  uint32_t max_s = corium::Wifi::downMaxS();
  if (max_s && down_s > max_s) {
    corium::Log::console(PSTR("WiFi: Down for %lus, restarting"), down_s);
    restart_soon(now, 100);
  }
}

static void note_reconnected(unsigned long now) {
  if (s_lost_at > 0) {
    unsigned long down_s = (now - s_lost_at) / 1000;
    corium::Log::console(PSTR("WiFi: Reconnected after %lus"), down_s);
  }
  corium::Wifi::go(corium::Wifi::ONLINE, PSTR("link up"));
}

static void reprobe_saved_network(unsigned long now) {
  if (corium::Wifi::apHeld(now)) return;   // a probe's STA mode would drop the AP
  uint32_t max_s = corium::Wifi::downMaxS();
  if (corium::Wifi::connected) {
    corium::Log::console(PSTR("WiFi: Saved network back, restarting to reconnect"));
    restart_soon(now, 500);
  } else if (max_s && now - CORIUM_MS_START > max_s * 1000UL) {   // ms since boot
    corium::Log::console(PSTR("WiFi: Offline %lus, restarting to retry"), now / 1000);
    restart_soon(now, 500);
  } else if (s_offline_retry_at == 0) {
    s_offline_retry_at = now + WIFI_OFFLINE_RETRY_MS;
  } else if (corium::reached(now, s_offline_retry_at)) {
    s_offline_retry_at = now + WIFI_OFFLINE_RETRY_MS;
    corium::Log::console(PSTR("WiFi: Offline - re-probing saved network"));
    WiFi.mode(WIFI_STA);
    (void)WiFi.begin();   // the link watch reads the status after
  }
}

static void clear_backups_once_stable() {
  corium::Wifi::backupGraceDone = true;
  if (corium::Fs::exists(WIFI_BACKUP_PATH) && corium::Fs::remove(WIFI_BACKUP_PATH)) {
    corium::Log::console(PSTR("WiFi: New creds stable for 5 min, backup cleared"));
  }
  if (corium::Fs::exists(NET_BACKUP_PATH) && corium::Fs::remove(NET_BACKUP_PATH)) {
    corium::Log::console(PSTR("WiFi: Static IP stable for 5 min, net backup cleared"));
  }
}

static void watch_link(wl_status_t st, unsigned long now) {
  if (!corium::Wifi::disabled) {
    if (st != WL_CONNECTED) track_outage(now);
    else if (corium::Wifi::state() != corium::Wifi::ONLINE) note_reconnected(now);
  } else if (!corium::Wifi::offlineForced) {
    reprobe_saved_network(now);
  }
}

bool corium::Wifi::look(unsigned long now) {
  wl_status_t st = WiFi.status();
  bool prev = corium::Wifi::connected;
  corium::Wifi::connected = (st == WL_CONNECTED);
  if (corium::Wifi::connected) refresh_link_cache(!prev, now);
  else corium::Wifi::rssi = 0;

  // A join test owns the link until it's done, offline too
  if (!corium::Wifi::testing()) watch_link(st, now);

  if (!corium::Wifi::backupGraceDone && corium::Wifi::connected && now - CORIUM_MS_START > WIFI_BACKUP_MS) clear_backups_once_stable();
  return corium::Wifi::connected != prev;
}
