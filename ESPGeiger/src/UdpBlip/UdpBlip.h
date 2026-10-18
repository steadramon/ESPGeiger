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
#ifndef UDP_BLIP_H
#define UDP_BLIP_H

#include <Arduino.h>
#include <IPAddress.h>
#include "Corium/Module/EGModule.h"
#include "BlipPacket.h"

class WiFiUDP;

#ifndef UDPBLIP_DEFAULT_GROUP
#define UDPBLIP_DEFAULT_GROUP "239.255.86.86"
#endif
#ifndef UDPBLIP_DEFAULT_PORT
#define UDPBLIP_DEFAULT_PORT  57340
#endif
#ifndef UDPBLIP_STATS_INTERVAL_MS
#define UDPBLIP_STATS_INTERVAL_MS 30000UL
#endif
// env every 2nd stats cycle
#define UDPBLIP_ENV_PERIOD_MS (2 * (UDPBLIP_STATS_INTERVAL_MS + 2000UL))
#ifndef UDPBLIP_FAIL_COOLDOWN_MS
#define UDPBLIP_FAIL_COOLDOWN_MS 10000UL
#endif
#ifndef UDPBLIP_STATS_JITTER_MS
#define UDPBLIP_STATS_JITTER_MS 7500UL
#endif
#ifndef UDPBLIP_FAIL_BACKOFF
#define UDPBLIP_FAIL_BACKOFF 8
#endif
#ifndef UDPBLIP_CLICK_MIN_INTERVAL_MS
#define UDPBLIP_CLICK_MIN_INTERVAL_MS 50
#endif
#ifndef UDPBLIP_CLICK_BURST_TOKENS
#define UDPBLIP_CLICK_BURST_TOKENS 5
#endif

class UdpBlipModule : public EGModule {
public:
  static UdpBlipModule& getInstance() {
    static UdpBlipModule instance;
    return instance;
  }
  const char* name() override { return PSTR("udpblip"); }
  bool requires_wifi() override { return true; }
  bool has_tick() override { return true; }
  bool has_loop() override { return true; }
  uint16_t loop_interval_ms() override;
  uint16_t warmup_seconds() override { return 0; }
  void begin() override;
  void s_tick(unsigned long now_s) override;
  void loop(unsigned long now) override;
  const EGPrefGroup* prefs_group() override;
  void on_prefs_saved() override;
  uint8_t display_order() override { return 20; }
  size_t status_json(char* buf, size_t cap, unsigned long now) override;
  void notifyClick(unsigned long now_ms);

private:
  UdpBlipModule() {}

  void readPrefs();
  void teardown();
  bool ensureUdp();
  void emitClick(uint32_t counter, uint32_t ts_ms);
  void tryEmitClick(unsigned long now_ms);
  void emitRad(uint32_t now);
  void emitSys(uint32_t now);
  void emitEnv(uint32_t now);
#ifdef ESPG_HV_ADC
  void emitHv(uint32_t now);
#endif
  bool sendPacket(const uint8_t* buf, size_t len);
  const char* path(const char* suffix);   // suffix flash
  void announce_mdns();

  WiFiUDP*  _udp = nullptr;
  uint8_t   _mode = 0;
  IPAddress _group;
  uint16_t  _port = 0;
  uint16_t  _src_port = 0;
  unsigned long _last_clicks = 0;
  unsigned long _last_click_emit_ms = 0;
  unsigned long _last_token_ms = 0;
  uint32_t _bound_at_ms = 0;            // wifi session, rebind on change
  uint16_t      _stats_ticks = 0;      // s
  uint16_t      _backoff_ticks = 0;    // s
  uint8_t       _burst_tokens = UDPBLIP_CLICK_BURST_TOKENS;
  uint8_t   _fail_count = 0;
  bool      _mdns_done = false;
  bool      _sys_phase = false;
  uint8_t   _emit_phase = 0;
  // s_tick sets, loop() sends
  static constexpr uint8_t EMIT_RAD = 1 << 0;
  static constexpr uint8_t EMIT_HV  = 1 << 1;
  static constexpr uint8_t EMIT_SYS = 1 << 2;
  static constexpr uint8_t EMIT_ENV = 1 << 3;
  static constexpr uint8_t BIND     = 1 << 4;
  static constexpr uint8_t REBIND   = 1 << 5;
  uint8_t   _pending = 0;
  uint8_t   _click_packet[BLIP_CLICK_LEN] = {0};
  char      _path[BLIP_ARGS_AT] = {0};   // "/espg/<id>/" then the suffix
};

extern UdpBlipModule& udpblip;

#endif
