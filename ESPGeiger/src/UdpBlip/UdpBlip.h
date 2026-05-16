/*
  UdpBlip.h - OSC over UDP multicast click + stats broadcaster

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
#ifndef _UDP_BLIP_H
#define _UDP_BLIP_H

#include <Arduino.h>
#include <IPAddress.h>
#include "../Module/EGModule.h"

class WiFiUDP;

#ifndef UDPBLIP_DEFAULT_GROUP
#define UDPBLIP_DEFAULT_GROUP "239.255.86.86"
#endif
#ifndef UDPBLIP_DEFAULT_PORT
#define UDPBLIP_DEFAULT_PORT  "57340"
#endif
#ifndef UDPBLIP_STATS_INTERVAL_MS
#define UDPBLIP_STATS_INTERVAL_MS 30000UL
#endif
#ifndef UDPBLIP_STATS_JITTER_MS
#define UDPBLIP_STATS_JITTER_MS 7500UL
#endif
#ifndef UDPBLIP_FAIL_BACKOFF
#define UDPBLIP_FAIL_BACKOFF 8
#endif

class UdpBlipModule : public EGModule {
public:
  static UdpBlipModule& getInstance() {
    static UdpBlipModule instance;
    return instance;
  }
  const char* name() override { return "udpblip"; }
  bool requires_wifi() override { return true; }
  bool has_loop() override { return true; }
  uint16_t loop_interval_ms() override { return 50; }
  uint16_t warmup_seconds() override { return 0; }
  void begin() override;
  void loop(unsigned long now) override;
  const EGPrefGroup* prefs_group() override;
  void on_prefs_saved() override;
  uint8_t display_order() override { return 26; }
  size_t status_json(char* buf, size_t cap, unsigned long now) override;
  void notifyClick(unsigned long now_ms);

private:
  UdpBlipModule() {}

  void readPrefs();
  void teardown();
  bool ensureUdp();
  void emitClick(uint32_t counter, uint32_t ts_ms);
  void emitRad(uint32_t now);
  void emitSys(uint32_t now);
#ifdef ESPG_HV_ADC
  void emitHv(uint32_t now);
#endif
  bool sendPacket(const uint8_t* buf, size_t len);
  void announce_mdns();

  WiFiUDP*  _udp = nullptr;
  uint8_t   _mode = 0;
  IPAddress _group;
  uint16_t  _port = 0;
  uint16_t  _src_port = 0;
  unsigned long _last_clicks = 0;
  unsigned long _last_stats_ms = 0;
  uint8_t   _fail_count = 0;
  bool      _mdns_done = false;
  bool      _sys_phase = false;
  uint8_t   _emit_phase = 0;
  char      _click_path[20] = {0};
  char      _rad_path[20]   = {0};
  char      _sys_path[20]   = {0};
#ifdef ESPG_HV_ADC
  char      _hv_path[20]    = {0};
#endif
};

extern UdpBlipModule& udpblip;

#endif
