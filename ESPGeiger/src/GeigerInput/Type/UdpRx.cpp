/*
  GeigerInput/Type/UdpRx.cpp - OSC over UDP multicast receiver

  Copyright (C) 2026 @steadramon
*/
#include "UdpRx.h"

#if GEIGER_IS_UDPRX(GEIGER_TYPE)

#include "../../Counter/Counter.h"
#include "../../Logger/Logger.h"
#include "../../Prefs/EGPrefs.h"
#include "../../Util/DeviceInfo.h"
#include "../../Util/Wifi.h"
#include <WiFiUdp.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

extern Counter gcounter;

// Routed back to the live instance by InputPrefs::on_prefs_saved (see
// udprx_notify_prefs_saved below).
static GeigerUdpRx* s_udprx_instance = nullptr;

// Path layout: "/espg/XXXXXX/click" or ".../stats", 18 chars + NUL + pad
// to 20 bytes. Fixed offsets let us skip strchr/regex on every packet.
static constexpr const char PATH_PREFIX[]  = "/espg/";
static constexpr size_t     PATH_PREFIX_LEN = 6;
static constexpr size_t     CHIPID_OFFSET   = 6;
static constexpr size_t     CHIPID_LEN      = 6;
static constexpr size_t     SUFFIX_OFFSET   = 13;
static constexpr size_t     PATH_FULL_LEN   = 20;
static constexpr const char SUFFIX_CLICK[]  = "click";
static constexpr const char SUFFIX_STATS[]  = "stats";
static constexpr const char TAG_IIF[]       = ",iif";

static inline uint32_t rd_i32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

static inline float rd_f32(const uint8_t* p) {
  uint32_t u = rd_i32(p);
  float f;
  memcpy(&f, &u, 4);
  return f;
}

// OSC string length including NUL + 4-byte padding. 0 on overflow.
static size_t osc_str_len(const uint8_t* p, size_t maxLen) {
  for (size_t i = 0; i < maxLen; i++) {
    if (p[i] == 0) {
      return (i + 4) & ~3u;
    }
  }
  return 0;
}

// Bridge from InputPrefs::on_prefs_saved (we piggyback on the "input" pref
// group rather than registering our own module).
extern "C" void udprx_notify_prefs_saved() {
  if (s_udprx_instance) s_udprx_instance->on_prefs_saved();
}

GeigerUdpRx::GeigerUdpRx() { s_udprx_instance = this; }
GeigerUdpRx::~GeigerUdpRx() { s_udprx_instance = nullptr; teardownUdp(); }

void GeigerUdpRx::begin() {
  readPrefs();
  _last_seen_count = _local_count;
  Log::console(PSTR("UdpRx: mode=%u group=%s port=%u rxmode=%u"),
               _mode, EGPrefs::getString("input", "udprx_group"),
               _port, _rx_mode);
}

void GeigerUdpRx::readPrefs() {
  _mode = (uint8_t)EGPrefs::getUInt("input", "udprx_mode");
  if (_mode > 1) _mode = 0;   // legacy: mode 2 (auto) folded into mode 0 + empty chipid
  const char* pin = EGPrefs::getString("input", "udprx_chipid");
  size_t n = strlen(pin);
  if (n > sizeof(_pinned_chipid) - 1) n = sizeof(_pinned_chipid) - 1;
  memcpy(_pinned_chipid, pin, n);
  _pinned_chipid[n] = '\0';

  const char* g = EGPrefs::getString("input", "udprx_group");
  if (!g || !g[0] || !_group.fromString(g)) {
    _group.fromString(UDPRX_DEFAULT_GROUP);
  }
  uint32_t p = EGPrefs::getUInt("input", "udprx_port");
  _port = (p > 0 && p < 65536) ? (uint16_t)p : 57340;
  uint32_t rm = EGPrefs::getUInt("input", "udprx_rxmode");
  _rx_mode = (rm <= 2) ? (uint8_t)rm : 1;
}

// Listen interval forced to 1: the SDK default of 3 dropped ~50% of
// multicast frames between DTIM windows. rx_mode 0/1/2 = light/modem/none.
static void apply_rx_sleep_mode(uint8_t rx_mode) {
#ifdef ESP8266
  switch (rx_mode) {
    case 0: WiFi.setSleepMode(WIFI_LIGHT_SLEEP, 1); break;
    case 2: WiFi.setSleepMode(WIFI_NONE_SLEEP);     break;
    case 1:
    default: WiFi.setSleepMode(WIFI_MODEM_SLEEP, 1); break;
  }
#else
  switch (rx_mode) {
    case 0: WiFi.setSleep(WIFI_PS_MAX_MODEM); break;
    case 2: WiFi.setSleep(WIFI_PS_NONE);      break;
    case 1:
    default: WiFi.setSleep(WIFI_PS_MIN_MODEM); break;
  }
#endif
}

void GeigerUdpRx::on_prefs_saved() {
  uint8_t  old_mode = _mode;
  uint16_t old_port = _port;
  uint8_t  old_rx_mode = _rx_mode;
  IPAddress old_group = _group;
  readPrefs();
  bool net_changed = (old_port != _port) || !(old_group == _group);
  if (net_changed) {
    teardownUdp();
  }
  // Mode change in isolation just affects the filter; UDP socket can stay.
  if (old_mode != _mode) {
    _locked_chipid[0] = '\0';   // re-latch on next click in auto mode
  }
  if (old_rx_mode != _rx_mode && _udp) {
    apply_rx_sleep_mode(_rx_mode);
  }
  Log::console(PSTR("UdpRx: prefs saved, mode=%u port=%u rxmode=%u"),
               _mode, _port, _rx_mode);
}

void GeigerUdpRx::teardownUdp() {
  if (_udp) {
    _udp->stop();
    delete _udp;
    _udp = nullptr;
  }
}

bool GeigerUdpRx::ensureUdp() {
  if (_udp) return true;
  if (!Wifi::connected) return false;
  _udp = new WiFiUDP();
  if (!_udp) {
    Log::console(PSTR("UdpRx: WiFiUDP alloc failed"));
    return false;
  }
  apply_rx_sleep_mode(_rx_mode);
#ifdef ESP8266
  if (!_udp->beginMulticast(WiFi.localIP(), _group, _port)) {
    teardownUdp();
    return false;
  }
#else
  if (!_udp->beginMulticast(_group, _port)) {
    teardownUdp();
    return false;
  }
#endif
  Log::console(PSTR("UdpRx: joined %s:%u"), EGPrefs::getString("input", "udprx_group"), _port);
  return true;
}

bool GeigerUdpRx::acceptChipid(const char* src_chipid) {
  if (_mode == 1) return true;   // sum
  // mode 0: pinned chipid wins; if blank, latch the first chipid heard.
  if (_pinned_chipid[0]) {
    return memcmp(_pinned_chipid, src_chipid, CHIPID_LEN) == 0;
  }
  if (_locked_chipid[0] == '\0') {
    memcpy(_locked_chipid, src_chipid, CHIPID_LEN);
    _locked_chipid[CHIPID_LEN] = '\0';
    Log::console(PSTR("UdpRx: locked to %s"), _locked_chipid);
    return true;
  }
  return memcmp(_locked_chipid, src_chipid, CHIPID_LEN) == 0;
}

GeigerUdpRx::ProducerRecord* GeigerUdpRx::findOrAllocProducer(const char* chipid,
                                                              uint32_t now_ms) {
  for (uint8_t i = 0; i < _producers_seen; i++) {
    if (memcmp(_producers[i].chipid, chipid, CHIPID_LEN) == 0) {
      _producers[i].last_seen_ms = now_ms;
      return &_producers[i];
    }
  }
  // LRU-evict when the 8 slots are full.
  uint8_t slot;
  if (_producers_seen < UDPRX_PRODUCER_SLOTS) {
    slot = _producers_seen++;
  } else {
    slot = 0;
    uint32_t oldest = _producers[0].last_seen_ms;
    for (uint8_t i = 1; i < UDPRX_PRODUCER_SLOTS; i++) {
      if (_producers[i].last_seen_ms < oldest) {
        slot = i;
        oldest = _producers[i].last_seen_ms;
      }
    }
  }
  memcpy(_producers[slot].chipid, chipid, CHIPID_LEN);
  _producers[slot].chipid[CHIPID_LEN] = '\0';
  _producers[slot].chipid[CHIPID_LEN + 1] = '\0';
  _producers[slot].last_seen_ms = now_ms;
  _producers[slot].click_count  = 0;
  _producers[slot].last_counter = 0;
  _producers[slot].last_ts_ms   = 0;
  _producers[slot].last_cpm_x10 = 0;
  _producers[slot].state        = 0;
  return &_producers[slot];
}

void GeigerUdpRx::processClick(const uint8_t* buf, size_t len, ProducerRecord* p, uint32_t now_ms) {
  if (len < PATH_FULL_LEN + 8 + 12) return;
  if (buf[CHIPID_OFFSET + CHIPID_LEN] != '/') return;
  if (buf[SUFFIX_OFFSET + 5] != '\0') return;
  if (memcmp(buf + PATH_FULL_LEN, TAG_IIF, 4) != 0) return;

  const uint8_t* args = buf + PATH_FULL_LEN + 8;
  uint32_t producer_counter = rd_i32(args);
  uint32_t producer_ts      = rd_i32(args + 4);
  float    cps              = rd_f32(args + 8);

  // Gap-fill tiers: <=1024 instant credit, 1024..65535 rate-drained over
  // dt_ms, bigger = resync, small backwards = reorder of an already-credited
  // future click (silently dropped to avoid double-count).
  uint32_t credit = 1;
  uint32_t gap_credit = 0;
  bool     advance_anchor = true;
  if (p->click_count > 0) {
    if (producer_counter == p->last_counter) {
      credit = 0;   // dup
    } else if (producer_counter > p->last_counter) {
      uint32_t gap = producer_counter - p->last_counter - 1;
      if (gap == 0) {
        // perfect sequence
      } else if (gap <= 1024) {
        credit += gap;
        gap_credit = gap;
      } else if (gap <= 65535) {
        uint32_t dt_ms = producer_ts - p->last_ts_ms;
        if (dt_ms > 100 && dt_ms < 60000) {
          _drain_pending += (float)gap;
          float r = (float)gap * 1000.0f / (float)dt_ms;
          if (r > _drain_rate) _drain_rate = r;
        } else {
          _resync_count++;
          Log::console(PSTR("UdpRx: %s drain rejected, gap=%u dt=%ums"),
                       p->chipid, (unsigned)gap, (unsigned)dt_ms);
        }
      } else {
        _resync_count++;
        Log::console(PSTR("UdpRx: %s resync, gap=%u (counter %u -> %u)"),
                     p->chipid, (unsigned)gap,
                     (unsigned)p->last_counter, (unsigned)producer_counter);
      }
    } else {
      // Backwards. Small = UDP reorder (we already gap-filled this number
      // when the future packet arrived). Big = producer reboot.
      uint32_t back = p->last_counter - producer_counter;
      if (back > 1024) {
        _resync_count++;
        Log::console(PSTR("UdpRx: %s counter reset (%u -> %u)"),
                     p->chipid,
                     (unsigned)p->last_counter, (unsigned)producer_counter);
      } else {
        credit = 0;
        advance_anchor = false;
      }
    }
  }
  if (credit > 0) {
    _packets_accepted++;
    _last_click_ms  = now_ms;
    _gap_filled    += gap_credit;
    gcounter.queueBlip(credit);
  }
  _local_count    += credit;
  p->click_count  += credit;
  if (advance_anchor) {
    p->last_counter = producer_counter;
    p->last_ts_ms   = producer_ts;
  }
  p->last_cpm_x10  = (uint16_t)(cps * 600.0f);   // cps * 60 * 10 fixed
}

void GeigerUdpRx::processStats(const uint8_t* buf, size_t len, ProducerRecord* p, uint32_t now_ms) {
  if (len < PATH_FULL_LEN + 8 + 12) return;
  if (buf[CHIPID_OFFSET + CHIPID_LEN] != '/') return;
  if (buf[SUFFIX_OFFSET + 5] != '\0') return;

  const uint8_t* args = buf + PATH_FULL_LEN + 8;
  float cpm = rd_f32(args);
  const uint8_t* p_state = args + 12;   // skip usv (4) + hv (4)
  size_t state_pad = osc_str_len(p_state, len - (PATH_FULL_LEN + 8 + 12));
  if (state_pad == 0) return;
  uint8_t state_code = 0;
  switch ((char)p_state[0]) {
    case 'w': state_code = p_state[1] == 'a' ? 3 : 1; break;  // warning vs warming
    case 'h': state_code = 2; break;
    case 'a': state_code = 4; break;
  }
  p->last_cpm_x10 = (uint16_t)(cpm * 10.0f);
  p->state = state_code;
  (void)now_ms;
}

void GeigerUdpRx::processDatagram(uint8_t* buf, size_t len) {
  if (len < 8) return;
  bool is_bundle = (memcmp(buf, "#bundle", 8) == 0);

  // Bundle first-msg offset: header 8 + timetag 8 + size 4 = 20. All messages
  // in a bundle share one producer chipid; we check once and reuse.
  const size_t prefix_off = is_bundle ? 20 : 0;
  const size_t chipid_off = prefix_off + CHIPID_OFFSET;
  if (len < chipid_off + CHIPID_LEN + 1) return;
  if (memcmp(buf + prefix_off, PATH_PREFIX, PATH_PREFIX_LEN) != 0) return;
  if (buf[chipid_off + CHIPID_LEN] != '/') return;
  char src[CHIPID_LEN + 1];
  memcpy(src, buf + chipid_off, CHIPID_LEN);
  src[CHIPID_LEN] = '\0';
  if (!acceptChipid(src)) return;

  uint32_t now_ms = millis();
  ProducerRecord* p = findOrAllocProducer(src, now_ms);
  _last_packet_ms = now_ms;

  if (is_bundle) {
    // Producer only bundles /click; trust the invariant.
    size_t off = 16;
    while (off + 4 <= len) {
      uint32_t msg_size = rd_i32(buf + off);
      off += 4;
      if (msg_size == 0 || off + msg_size > len) break;
      processClick(buf + off, msg_size, p, now_ms);
      off += msg_size;
    }
    return;
  }
  // Single message: first suffix byte discriminates ('c' click, 's' stats).
  if (len < PATH_FULL_LEN + 4) return;
  uint8_t s0 = buf[SUFFIX_OFFSET];
  if (s0 == 'c') {
    if (memcmp(buf + SUFFIX_OFFSET, SUFFIX_CLICK, 5) != 0) return;
    processClick(buf, len, p, now_ms);
  } else if (s0 == 's') {
    if (memcmp(buf + SUFFIX_OFFSET, SUFFIX_STATS, 5) != 0) return;
    processStats(buf, len, p, now_ms);
  }
}

void GeigerUdpRx::loop() {
  if (!ensureUdp()) return;
  IPAddress me = WiFi.localIP();
  uint8_t buf[768];
  int sz;
  while ((sz = _udp->parsePacket()) > 0) {
    // Self-loop reject: skip our own broadcasts before reading any bytes.
    if (_udp->remoteIP() == me) {
      while (_udp->available()) _udp->read();
      continue;
    }
    if ((size_t)sz > sizeof(buf)) {
      while (_udp->available()) _udp->read();
      continue;
    }
    int got = _udp->read(buf, sz);
    if (got <= 0) continue;
    processDatagram(buf, (size_t)got);
  }
}

void GeigerUdpRx::secondTicker() {
  // Float-accumulator drain matching GeigerSerial::partial_clicks shape.
  if (_drain_rate <= 0.0f) return;
  float step = _drain_rate;
  if (step > _drain_pending) step = _drain_pending;
  _drain_pending -= step;
  _drain_partial += step;
  if (_drain_pending <= 0.0f) {
    _drain_pending = 0.0f;
    _drain_rate    = 0.0f;
  }
  if (_drain_partial >= 1.0f) {
    uint32_t emit = (uint32_t)_drain_partial;
    _drain_partial -= emit;
    _local_count   += emit;
    _gap_filled    += emit;
  }
}

int GeigerUdpRx::collect() {
  uint32_t now = _local_count;
  int delta = (int)(now - _last_seen_count);
  _last_seen_count = now;
  return delta;
}

bool GeigerUdpRx::isHealthy() const {
  return _producers_seen > 0;
}

const GeigerUdpRx::ProducerRecord* GeigerUdpRx::producer_at(uint8_t i) const {
  return (i < _producers_seen) ? &_producers[i] : nullptr;
}

const char* GeigerUdpRx::locked_chipid() const {
  if (_mode == 1) return nullptr;   // sum mode: caller uses producer_count
  if (_pinned_chipid[0]) return _pinned_chipid;
  if (_locked_chipid[0]) return _locked_chipid;
  return nullptr;
}

#endif // GEIGER_IS_UDPRX
