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
#ifndef _GEIGER_UDPRX_H
#define _GEIGER_UDPRX_H

#include "../GeigerInput.h"
#include <IPAddress.h>

class WiFiUDP;

#ifndef UDPRX_DEFAULT_GROUP
  #ifdef UDPBLIP_DEFAULT_GROUP
    #define UDPRX_DEFAULT_GROUP UDPBLIP_DEFAULT_GROUP
  #else
    #define UDPRX_DEFAULT_GROUP "239.255.86.86"
  #endif
#endif
#ifndef UDPRX_DEFAULT_PORT
#define UDPRX_DEFAULT_PORT  "57340"
#endif
#ifndef UDPRX_PRODUCER_SLOTS
#define UDPRX_PRODUCER_SLOTS 8
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

  void on_prefs_saved();

  struct ProducerRecord {
    char     chipid[8];       // 6 hex + NUL + pad
    uint32_t last_seen_ms;
    uint32_t click_count;     // credited locally, including gap-fills
    uint32_t last_counter;    // producer's lifetime click number, for gap detection
    uint32_t last_ts_ms;      // producer's millis() at last /click, for rate-drain
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
  // Bumped on any accepted message (click or stats). Use for "producer
  // alive?" UI; survives quiet feeds where stats still arrives every 60s.
  uint32_t last_packet_ms()   const { return _last_packet_ms; }
  // Gap-fill credits as % of total credited clicks, x10 (0..1000).
  uint16_t loss_pct_x10() const {
    uint32_t total = _local_count;
    if (total == 0) return 0;
    uint32_t g = _gap_filled;
    if (g >= total) return 1000;
    return (uint16_t)((g * 1000u) / total);
  }

private:
  void readPrefs();
  void teardownUdp();
  bool ensureUdp();
  bool acceptChipid(const char* src_chipid);
  void processDatagram(uint8_t* buf, size_t len);
  void processClick(const uint8_t* buf, size_t len, ProducerRecord* p, uint32_t now_ms);
  ProducerRecord* findOrAllocProducer(const char* chipid, uint32_t now_ms);

  WiFiUDP*  _udp = nullptr;
  IPAddress _group;
  uint16_t  _port = 57340;
  uint8_t   _mode = 2;          // 0=pin 1=sum 2=auto
  char      _pinned_chipid[8] = {0};
  char      _locked_chipid[8] = {0};
  uint8_t   _rx_mode = 1;       // 0=light 1=modem 2=none

  volatile uint32_t _local_count = 0;
  uint32_t _last_seen_count = 0;
  uint32_t _packets_accepted = 0;
  uint32_t _gap_filled = 0;
  uint32_t _resync_count = 0;
  uint32_t _last_click_ms = 0;
  uint32_t _last_packet_ms = 0;
  // Rate-drain for large gaps. Mirrors GeigerSerial::partial_clicks:
  // pending decrements as rate is drained per second, partial carries
  // the sub-1 fraction between emits.
  float _drain_pending = 0.0f;
  float _drain_rate    = 0.0f;
  float _drain_partial = 0.0f;

  ProducerRecord _producers[UDPRX_PRODUCER_SLOTS] = {};
  uint8_t _producers_seen = 0;
  unsigned long _bound_at_ms = 0;
};

#endif
