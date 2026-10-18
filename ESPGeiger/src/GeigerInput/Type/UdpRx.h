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
#include <IPAddress.h>

class WiFiUDP;

#ifndef UDPRX_DEFAULT_GROUP
#define UDPRX_DEFAULT_GROUP UDPBLIP_DEFAULT_GROUP
#endif
#ifndef UDPRX_DEFAULT_PORT
#define UDPRX_DEFAULT_PORT  UDPBLIP_DEFAULT_PORT
#endif
#ifndef UDPRX_PRODUCER_SLOTS
#define UDPRX_PRODUCER_SLOTS 8
#endif
#ifndef UDPRX_GAPFILL_QUEUE
#define UDPRX_GAPFILL_QUEUE 16
#endif
#ifndef UDPRX_POLL_SKIP
#define UDPRX_POLL_SKIP 500      // loop passes per drain
#endif
class GeigerUdpRx : public GeigerInput {
public:
  GeigerUdpRx();
  ~GeigerUdpRx() override;

  void begin() override;
  void loop() override;
  void secondTicker() override;
  int  collect() override;
  bool isHealthy() const override;
  EGMeasureRows measurements() override;

  void on_prefs_saved();

  struct ProducerRecord {
    char     chipid[BLIP_ID_LEN + 2];   // NUL + pad
    uint32_t last_seen_ms;
    uint32_t click_count;
    uint32_t last_counter;    // producer's count, gap detect
    uint32_t last_ts_ms;      // producer clock, deltas only
  };

  uint8_t producer_count() const { return _producers_seen; }
  const char* locked_chipid() const;
  uint8_t mode() const { return _mode; }
  IPAddress group() const { return _group; }
  uint16_t  port()  const { return _port; }

  uint32_t packets_accepted() const { return _packets_accepted; }
  uint32_t gap_filled()       const { return _gap_filled; }
  uint32_t resync_count()     const { return _resync_count; }
  uint32_t last_click_ms()    const { return _last_click_ms; }
  // any message
  uint32_t last_packet_ms()   const { return _last_packet_ms; }
  uint16_t loss_pct_x10() const {
    uint32_t total = _local_count;
    if (total == 0) return 0;
    uint32_t g = _gap_filled;
    if (g >= total) return 1000;
    return (uint16_t)((g * 1000u) / total);
  }
  uint32_t quiet_s() const;   // 0 before the first
  uint16_t rebind_count() const       { return _rebind_count; }
  uint16_t refresh_count() const      { return _refresh_count; }
  uint16_t refresh_noop_count() const { return _refresh_noop_count; }

private:
  void readPrefs();
  void teardownUdp();
  bool ensureUdp();
  bool ensureBound(uint32_t now);
  void climbRecoveryLadder(uint32_t now);
  void readDatagrams();
  void fireDueGapfills();
  bool acceptChipid(const char* src_chipid);
  void processDatagram(uint8_t* buf, size_t len);
  void processClick(const uint8_t* buf, size_t len, ProducerRecord* p, uint32_t now_ms);
  uint32_t creditForwardGap(const ProducerRecord* p, uint32_t producer_counter, uint32_t producer_ts);
  bool producerRestarted(const ProducerRecord* p, uint32_t producer_counter, uint32_t producer_ts);
  void creditClicks(uint32_t credit, uint32_t gap_credit, const ProducerRecord* p,
                    uint32_t producer_ts, uint32_t now_ms);
  void scheduleGapfill(uint32_t gap_credit, uint32_t window_ms, uint32_t now_ms);
  void processEnv(const uint8_t* buf, size_t len);
  ProducerRecord* findOrAllocProducer(const char* chipid, uint32_t now_ms);

  WiFiUDP*  _udp = nullptr;
  IPAddress _group;
  uint16_t  _port = UDPRX_DEFAULT_PORT;
  uint8_t   _mode = 2;          // 0=pin 1=sum, 2 legacy auto
  char      _pinned_chipid[BLIP_ID_LEN + 1] = {0};
  char      _locked_chipid[BLIP_ID_LEN + 1] = {0};
  char      _env_chipid[BLIP_ID_LEN + 1] = {0};
  uint8_t   _rx_mode = 1;       // 0=light 1=modem 2=none

  volatile uint32_t _local_count = 0;
  uint32_t _last_seen_count = 0;
  uint32_t _packets_accepted = 0;
  uint32_t _gap_filled = 0;
  uint32_t _resync_count = 0;
  uint32_t _last_click_ms = 0;
  uint32_t _last_packet_ms = 0;
  float _drain_pending = 0.0f;
  float _drain_rate    = 0.0f;
  float _drain_partial = 0.0f;
  bool  _draining      = false;   // int guard

  ProducerRecord _producers[UDPRX_PRODUCER_SLOTS] = {};
  uint8_t _producers_seen = 0;
  uint32_t _bound_at_ms = 0;
  unsigned long _last_igmp_refresh_ms = 0;

  void do_refresh();
  unsigned long _recovery_at_ms = 0;   // 0 = idle
  uint16_t _recovery_steps = 0;
  uint32_t _ps_bounce_until_ms = 0;    // 0 = idle
  // Diags
  uint16_t _rebind_count = 0;
  uint16_t _refresh_count = 0;
  uint16_t _refresh_noop_count = 0;    // no netif

  uint32_t _gapfill_due_ms[UDPRX_GAPFILL_QUEUE] = {0};
  uint8_t  _gapfill_count = 0;
  uint32_t _gapfill_dropped = 0;
};

#endif
