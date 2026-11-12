/*
  GeigerInput/Type/UdpRx.h - OSC over UDP multicast receiver.

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
#ifndef GEIGER_UDPRX_H
#define GEIGER_UDPRX_H

#include "GeigerInput/GeigerInput.h"
#include "UdpBlip/UdpBlip.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/Net/IgmpRefresh.h"
#include <IPAddress.h>

class WiFiUDP;

#ifndef UDPRX_DEFAULT_GROUP
#define UDPRX_DEFAULT_GROUP UDPBLIP_DEFAULT_GROUP
#endif
#ifndef UDPRX_DEFAULT_PORT
#define UDPRX_DEFAULT_PORT  UDPBLIP_DEFAULT_PORT
#endif
// senders tracked, oldest evicted
#ifndef UDPRX_PRODUCER_SLOTS
#define UDPRX_PRODUCER_SLOTS 8
#endif
// lost clicks replayed as blips, max pending
#ifndef UDPRX_GAPFILL_QUEUE
#define UDPRX_GAPFILL_QUEUE 16
#endif
#ifndef UDPRX_POLL_MS
#define UDPRX_POLL_MS 4          // ms per drain, ~500 8266 passes
#endif
#ifndef UDPRX_QUIET_MS
#define UDPRX_QUIET_MS (3 * UDPBLIP_STATS_INTERVAL_MS)   // 3 heartbeats
#endif
#ifndef UDPRX_LOG_MAX
#define UDPRX_LOG_MAX 4          // resync lines a minute
#endif
#define UDPRX_DRAIN_MAX 65535    // biggest gap drained, and all pending
// 2 lost /env, third up to half a period late
#ifndef UDPRX_ENV_TTL_MS
#define UDPRX_ENV_TTL_MS (3 * UDPBLIP_ENV_PERIOD_MS + UDPBLIP_ENV_PERIOD_MS / 2)
#endif
// pin pref and the wire, blank allowed
#define UDPRX_CHIPID_PAT "[0-9a-fA-F]{" STR(CHIPID_LEN) "}"

// Counts from UdpBlip senders, OSC /espg/<chipid>/click and /env
class GeigerUdpRx final : public GeigerInput {
public:
  GeigerUdpRx();
  ~GeigerUdpRx() override;

  void begin() override;
  void loop() override;
  void secondTicker() override;
  int  collect() override;
  bool isHealthy() const override;
  corium::MeasureRows measurements() override;

  void onPrefsSaved();

  struct ProducerRecord {
    char     chipid[BLIP_ID_LEN + 2];   // NUL + pad
    uint32_t last_seen_ms;
    uint32_t click_count;
    uint32_t last_counter;    // producer's count, gap detect
    uint32_t last_ts_ms;      // producer clock, deltas only
  };

  uint8_t producer_count() const { return _producers_seen; }
  const char* locked_chipid() const;
  enum : uint8_t { RX_ONE, RX_SUM };
  uint8_t mode() const { return _mode; }
  bool summing() const { return _mode == RX_SUM; }
  IPAddress group() const { return _group; }
  uint16_t  port()  const { return _port; }

  uint32_t packets_accepted() const { return _packets_accepted; }
  uint32_t gap_filled()       const { return _gap_filled; }
  uint32_t resync_count()     const { return _resync_count; }
  uint32_t last_click_ms()    const { return _last_click_ms; }
  // any message
  uint32_t last_packet_ms()   const { return _last_packet_ms; }
  uint16_t loss_pct_x10() const { return perMille(_gap_filled, _local_count); }
  uint32_t quiet_s() const;   // 0 before the first
  bool heard() const;
  uint32_t rebind_count() const       { return _rebind_count; }
  uint32_t refresh_count() const      { return _refresh_count; }
  uint32_t refresh_noop_count() const { return _refresh_noop_count; }
  // the last /env, 0 t 1 h 2 p; false once stale
  bool env(uint8_t i, float* v);

private:
  void readPrefs();
  void teardownUdp();
  bool ensureUdp();
  bool ensureBound(uint32_t now);
  void recover(uint32_t now);
  void readDatagrams();
  void fireDueGapfills();
  void unlatch();
  bool acceptChipid(const char* src_chipid, bool click);
  void processDatagram(uint8_t* buf, size_t len);
  void processClick(const uint8_t* buf, size_t len, ProducerRecord* p, uint32_t now_ms);
  uint32_t creditForwardGap(const ProducerRecord* p, uint32_t count, uint32_t ts_ms);
  bool producerRestarted(const ProducerRecord* p, uint32_t count, uint32_t ts_ms);
  bool logOk();
  void creditClicks(uint32_t credit, uint32_t gap_credit, const ProducerRecord* p,
                    uint32_t ts_ms, uint32_t now_ms);
  void scheduleGapfill(uint32_t gap_credit, uint32_t window_ms, uint32_t now_ms);
  void processEnv(const uint8_t* buf, size_t len);
  ProducerRecord* producer(const char* chipid, uint32_t now_ms);

  WiFiUDP*  _udp = nullptr;
  IPAddress _group;
  uint16_t  _port = UDPRX_DEFAULT_PORT;
  uint8_t   _mode = 2;          // legacy auto
  char      _pinned_chipid[BLIP_ID_LEN + 1] = {};
  char      _locked_chipid[BLIP_ID_LEN + 1] = {};
  char      _env_chipid[BLIP_ID_LEN + 1] = {};
  float     _env[3] = {NAN, NAN, NAN};
  corium::Stamp _env_at;
  uint8_t   _rx_mode = 1;       // 0=light 1=modem 2=none
  corium::IgmpRefresh::Join _joined;   // _group moves first on a change

  uint32_t _local_count = 0;
  uint32_t _collected = 0;
  uint32_t _packets_accepted = 0;
  uint32_t _gap_filled = 0;
  uint32_t _resync_count = 0;
  uint32_t _last_click_ms = 0;
  uint32_t _last_packet_ms = 0;
  uint32_t _last_packet_s = 0;   // uptime, quiet_s past the ms wrap
  corium::Stamp _heard;          // stale stays stale over the wrap
  float _drain_pending = 0.0f;
  float _drain_rate    = 0.0f;
  float _drain_partial = 0.0f;
  bool  _draining      = false;   // int guard
  uint32_t _log_at = 0;
  uint8_t  _log_left = UDPRX_LOG_MAX;

  ProducerRecord _producers[UDPRX_PRODUCER_SLOTS] = {};
  uint8_t _producers_seen = 0;
  uint32_t _bound_at_ms = 0;
  unsigned long _igmp_ms = 0;

  void do_refresh();
  unsigned long _recovery_at_ms = 0;   // 0 = idle
  uint32_t _recovery_steps = 0;
  corium::Deadline _ps_bounce;
  // Diags
  uint32_t _rebind_count = 0;
  uint32_t _refresh_count = 0;
  uint32_t _refresh_noop_count = 0;    // no netif

  uint32_t _gapfill_due_ms[UDPRX_GAPFILL_QUEUE] = {};
  uint8_t  _gapfill_count = 0;
};

#endif
