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
#ifndef UDPBLIP_FAIL_COOLDOWN_MS
// How long to sit in the fail-backoff state before resetting _fail_count
// and re-trying. Decoupled from STATS_INTERVAL_MS so telemetry cadence and
// recovery cadence can be tuned independently.
#define UDPBLIP_FAIL_COOLDOWN_MS 10000UL
#endif
#ifndef UDPBLIP_STATS_JITTER_MS
#define UDPBLIP_STATS_JITTER_MS 7500UL
#endif
#ifndef UDPBLIP_FAIL_BACKOFF
#define UDPBLIP_FAIL_BACKOFF 8
#endif
#ifndef UDPBLIP_CLICK_MIN_INTERVAL_MS
// Cap /click emit rate. Deferred clicks accumulate in the cumulative counter
// so the receiver gap-fills regardless. 50 ms = 20 pps cap (~1200 CPM
// unthrottled), ~0.6 % CPU at saturation.
#define UDPBLIP_CLICK_MIN_INTERVAL_MS 50
#endif
#ifndef UDPBLIP_CLICK_BURST_TOKENS
// Token-bucket depth. Idle time earns 1 token per CLICK_MIN_INTERVAL_MS up
// to this cap; each emit spends one. Lets a sudden cluster of clicks after
// idle fire as separate packets (visible blips on the receiver) before the
// steady-state throttle kicks in.
#define UDPBLIP_CLICK_BURST_TOKENS 5
#endif

class UdpBlipModule : public EGModule {
public:
  static UdpBlipModule& getInstance() {
    static UdpBlipModule instance;
    return instance;
  }
  const char* name() override { return "udpblip"; }
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
  uint8_t display_order() override { return 20; }  // first output: minimal config
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
  unsigned long _last_click_emit_ms = 0;
  unsigned long _last_token_ms = 0;
  uint16_t      _stats_ticks = 0;      // seconds since last /rad emit
  uint16_t      _backoff_ticks = 0;    // seconds in fail-backoff
  uint8_t       _burst_tokens = UDPBLIP_CLICK_BURST_TOKENS;
  uint8_t   _fail_count = 0;
  bool      _mdns_done = false;
  bool      _sys_phase = false;
  uint8_t   _emit_phase = 0;
  char      _click_path[20] = {0};
  uint8_t   _click_packet[32] = {0};  // /click OSC template; emitClick patches i32s at [24],[28]
  char      _rad_path[20]   = {0};
  char      _sys_path[20]   = {0};
#ifdef ESPG_HV_ADC
  char      _hv_path[20]    = {0};
#endif
};

extern UdpBlipModule& udpblip;

#endif
