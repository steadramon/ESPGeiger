/*
  GeigerInput/Type/UdpRx.cpp - OSC over UDP multicast receiver

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
#include "UdpRx.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Net/IgmpRefresh.h"

#if GEIGER_IS_UDPRX(GEIGER_TYPE)

#include "Counter/Counter.h"
#include "EnvSensor/EnvSensor.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Prefs/EGPrefs.h"
#include "GeigerPrefs/GeigerKeys.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/Net/Wifi.h"
#include <WiFiUdp.h>
#include <string.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

static GeigerUdpRx* s_udprx_instance = nullptr;

static constexpr const char PATH_PREFIX[]  = "/espg/";
static constexpr const char TAG_II[]        = ",ii";
static constexpr const char TAG_FFF[]       = ",fff";
// "env" includes its NUL
static constexpr const char SUFFIX_CLICK[]  = "click";
static constexpr const char SUFFIX_ENV[]    = "env";

static inline uint32_t rd_i32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

static inline float rd_f32(const uint8_t* p) {
  uint32_t bits = rd_i32(p);
  float f;
  memcpy(&f, &bits, sizeof(f));
  return f;
}

extern "C" void udprx_notify_prefs_saved() {
  if (s_udprx_instance) s_udprx_instance->on_prefs_saved();
}

GeigerUdpRx::GeigerUdpRx() { s_udprx_instance = this; }
GeigerUdpRx::~GeigerUdpRx() { s_udprx_instance = nullptr; teardownUdp(); }

void GeigerUdpRx::begin() {
  _poll_skip = UDPRX_POLL_SKIP - 1;
  readPrefs();
  _last_seen_count = _local_count;
  Log::console(PSTR("UdpRx: mode=%u group=%s port=%u rxmode=%u"),
               _mode, EGPrefs::getString("counter", EG_K_UDPRX_GROUP),
               _port, _rx_mode);
}

void GeigerUdpRx::readPrefs() {
  _mode = (uint8_t)EGPrefs::getUInt("counter", EG_K_UDPRX_MODE);
  if (_mode > 1) _mode = 0;   // legacy 2 = auto
  const char* pin = EGPrefs::getString("counter", EG_K_UDPRX_CHIPID);
  size_t n = strlen(pin);
  if (n && n != BLIP_ID_LEN) {   // a short pin would match nothing
    Log::console(PSTR("UdpRx: pin %s is not %u chars, ignored"), pin, (unsigned)BLIP_ID_LEN);
    n = 0;
  }
  memcpy(_pinned_chipid, pin, n);
  _pinned_chipid[n] = '\0';

  const char* g = EGPrefs::getString("counter", EG_K_UDPRX_GROUP);
  if (!g || !g[0] || !_group.fromString(g)) {
    _group.fromString(UDPRX_DEFAULT_GROUP);
  }
  uint32_t p = EGPrefs::getUInt("counter", EG_K_UDPRX_PORT);
  _port = (p > 0 && p < 65536) ? (uint16_t)p : UDPRX_DEFAULT_PORT;
  uint32_t rm = EGPrefs::getUInt("counter", EG_K_UDPRX_RXMODE);
  _rx_mode = (rm <= 2) ? (uint8_t)rm : 1;
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
  if (old_mode != _mode) {
    _locked_chipid[0] = '\0';   // re-latch next click
  }
  if (old_rx_mode != _rx_mode && _udp) {
    Wifi::rx_sleep(_rx_mode);
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
  Wifi::rx_sleep(_rx_mode);
  bool ok;
  if (Wifi::is_multicast(_group)) {
#ifdef ESP8266
    ok = _udp->beginMulticast(Wifi::local_ip, _group, _port);
#else
    ok = _udp->beginMulticast(_group, _port);
#endif
    if (ok) Log::console(PSTR("UdpRx: joined %s:%u"),
                          EGPrefs::getString("counter", EG_K_UDPRX_GROUP), _port);
  } else {
    ok = _udp->begin(_port);
    if (ok) Log::console(PSTR("UdpRx: listening unicast on :%u"), _port);
  }
  if (!ok) {
    teardownUdp();
    return false;
  }
  return true;
}

bool GeigerUdpRx::acceptChipid(const char* src_chipid) {
  if (_mode == 1) return true;   // sum
  // pinned chipid, else first heard
  if (_pinned_chipid[0]) {
    return memcmp(_pinned_chipid, src_chipid, BLIP_ID_LEN) == 0;
  }
  if (_locked_chipid[0] == '\0') {
    memcpy(_locked_chipid, src_chipid, BLIP_ID_LEN);
    _locked_chipid[BLIP_ID_LEN] = '\0';
    Log::console(PSTR("UdpRx: locked to %s"), _locked_chipid);
    return true;
  }
  return memcmp(_locked_chipid, src_chipid, BLIP_ID_LEN) == 0;
}

GeigerUdpRx::ProducerRecord* GeigerUdpRx::findOrAllocProducer(const char* chipid,
                                                              uint32_t now_ms) {
  for (uint8_t i = 0; i < _producers_seen; i++) {
    if (memcmp(_producers[i].chipid, chipid, BLIP_ID_LEN) == 0) {
      _producers[i].last_seen_ms = now_ms;
      return &_producers[i];
    }
  }
  // evict by age, wrap safe
  uint8_t slot;
  if (_producers_seen < UDPRX_PRODUCER_SLOTS) {
    slot = _producers_seen++;
  } else {
    slot = 0;
    uint32_t oldest = now_ms - _producers[0].last_seen_ms;
    for (uint8_t i = 1; i < UDPRX_PRODUCER_SLOTS; i++) {
      uint32_t age = now_ms - _producers[i].last_seen_ms;
      if (age > oldest) {
        slot = i;
        oldest = age;
      }
    }
  }
  memcpy(_producers[slot].chipid, chipid, BLIP_ID_LEN);
  _producers[slot].chipid[BLIP_ID_LEN] = '\0';
  _producers[slot].chipid[BLIP_ID_LEN + 1] = '\0';
  _producers[slot].last_seen_ms = now_ms;
  _producers[slot].click_count  = 0;
  _producers[slot].last_counter = 0;
  _producers[slot].last_ts_ms   = 0;
  return &_producers[slot];
}

// <= 1024 now, to 65535 drained, more = resync
uint32_t GeigerUdpRx::creditForwardGap(const ProducerRecord* p, uint32_t producer_counter,
                                       uint32_t producer_ts) {
  uint32_t gap = producer_counter - p->last_counter - 1;
  if (gap <= 1024) return gap;
  if (gap <= 65535) {
    uint32_t dt_ms = producer_ts - p->last_ts_ms;
    if (dt_ms > 100 && dt_ms < 60000) {
      _drain_pending += (float)gap;
      float r = (float)gap * 1000.0f / (float)dt_ms;
      if (r > _drain_rate) _drain_rate = r;
      _draining = true;
    } else {
      _resync_count++;
      Log::console(PSTR("UdpRx: %s drain rejected, gap=%u dt=%ums"),
                   p->chipid, (unsigned)gap, (unsigned)dt_ms);
    }
    return 0;
  }
  _resync_count++;
  Log::console(PSTR("UdpRx: %s resync, gap=%u (counter %u -> %u)"),
               p->chipid, (unsigned)gap,
               (unsigned)p->last_counter, (unsigned)producer_counter);
  return 0;
}

// small = reorder, big or ts back = producer reboot
bool GeigerUdpRx::producerRestarted(const ProducerRecord* p, uint32_t producer_counter,
                                    uint32_t producer_ts) {
  uint32_t back = p->last_counter - producer_counter;
  bool ts_reset = (int32_t)(producer_ts - p->last_ts_ms) < -60000;
  if (back > 1024 || ts_reset) {
    _resync_count++;
    Log::console(PSTR("UdpRx: %s counter reset (%u -> %u)"),
                 p->chipid,
                 (unsigned)p->last_counter, (unsigned)producer_counter);
    return true;
  }
  return false;
}

// Poisson spread over <= 50 ms
void GeigerUdpRx::scheduleGapfill(uint32_t gap_credit, uint32_t window_ms, uint32_t now_ms) {
  if (window_ms == 0 || window_ms > 50) window_ms = 50;
  uint32_t r = (uint32_t)micros();
  for (uint32_t i = 0; i < gap_credit; i++) {
    if (_gapfill_count >= UDPRX_GAPFILL_QUEUE) {
      _gapfill_dropped += (gap_credit - i);
      break;
    }
    r ^= r << 13; r ^= r >> 17; r ^= r << 5;
    _gapfill_due_ms[_gapfill_count++] = now_ms + (r % window_ms) + 1;
  }
}

void GeigerUdpRx::creditClicks(uint32_t credit, uint32_t gap_credit, const ProducerRecord* p,
                               uint32_t producer_ts, uint32_t now_ms) {
  _packets_accepted++;
  _last_click_ms  = now_ms;
  _gap_filled    += gap_credit;
  gcounter.dispatchReceiverBlip();
  if (gap_credit > 0) scheduleGapfill(gap_credit, producer_ts - p->last_ts_ms, now_ms);
  // batch, receiver timing would lie
  uint32_t now_us = (uint32_t)micros();
  if (credit == 1) {
    Counter::on_pulse_batch(1, now_us, 0);
  } else {
    uint32_t dt_ms = producer_ts - p->last_ts_ms;
    uint32_t span_us = ((int32_t)dt_ms > 0 && dt_ms < 60000)
                        ? dt_ms * 1000UL : 1000000UL;
    Counter::on_pulse_batch((uint16_t)credit, now_us, span_us);
  }
}

void GeigerUdpRx::processClick(const uint8_t* buf, size_t len, ProducerRecord* p, uint32_t now_ms) {
  // ",ii\0" + 2 x int32
  if (len < BLIP_CLICK_LEN) return;
  if (buf[BLIP_SUFFIX_AT + 5] != '\0') return;
  if (memcmp(buf + BLIP_ARGS_AT, TAG_II, 4) != 0) return;

  uint32_t producer_counter = rd_i32(buf + BLIP_CLICK_COUNT_AT);
  uint32_t producer_ts      = rd_i32(buf + BLIP_CLICK_MS_AT);

  uint32_t credit = 1;
  uint32_t gap_credit = 0;
  bool     advance_anchor = true;
  if (p->click_count > 0) {
    if (producer_counter == p->last_counter) {
      credit = 0;   // dup
    } else if (producer_counter > p->last_counter) {
      gap_credit = creditForwardGap(p, producer_counter, producer_ts);
      credit += gap_credit;
    } else if (!producerRestarted(p, producer_counter, producer_ts)) {
      credit = 0;
      advance_anchor = false;
    }
  }
  if (credit > 0) creditClicks(credit, gap_credit, p, producer_ts, now_ms);
  _local_count    += credit;
  p->click_count  += credit;
  if (advance_anchor) {
    p->last_counter = producer_counter;
    p->last_ts_ms   = producer_ts;
  }
}

void GeigerUdpRx::processDatagram(uint8_t* buf, size_t len) {
  if (len < BLIP_ARGS_AT + 4) return;
  if (buf[BLIP_ID_AT + BLIP_ID_LEN] != '/') return;
  if (memcmp(buf, PATH_PREFIX, BLIP_ID_AT) != 0) return;
  // own packets
  if (memcmp(buf + BLIP_ID_AT, DeviceInfo::chipid(), BLIP_ID_LEN) == 0) return;
  const char* src = (const char*)(buf + BLIP_ID_AT);
  if (!acceptChipid(src)) return;

  uint32_t now_ms = fast_millis();
  ProducerRecord* p = findOrAllocProducer(src, now_ms);
  _last_packet_ms = now_ms;

  // other paths only keep the producer alive
  const uint8_t* s = buf + BLIP_SUFFIX_AT;
  if (memcmp(s, SUFFIX_CLICK, 5) == 0) {
    processClick(buf, len, p, now_ms);
  } else if (memcmp(s, SUFFIX_ENV, 4) == 0) {
    processEnv(buf, len);
  }
}

void GeigerUdpRx::processEnv(const uint8_t* buf, size_t len) {
  // ",fff" padded to 8 + 3 x f32
  if (len < BLIP_ENV_LEN) return;
  if (memcmp(buf + BLIP_ARGS_AT, TAG_FFF, 4) != 0) return;
  // first env producer only
  const char* src = (const char*)(buf + BLIP_ID_AT);
  if (_env_chipid[0] == '\0') {
    memcpy(_env_chipid, src, BLIP_ID_LEN);
    _env_chipid[BLIP_ID_LEN] = '\0';
  } else if (memcmp(_env_chipid, src, BLIP_ID_LEN) != 0) {
    return;
  }
  const uint8_t* args = buf + BLIP_ENV_AT;
  float t = rd_f32(args);
  float h = rd_f32(args + 4);
  float p = rd_f32(args + 8);
  envsensor.setRemote(t, h, p);
}

void GeigerUdpRx::do_refresh() {
  if (IgmpRefresh::refresh()) _refresh_count++;
  else                        _refresh_noop_count++;
}

// rebind after a wifi reconnect, igmp is lost
bool GeigerUdpRx::ensureBound(uint32_t now) {
  if (_udp && Wifi::connected_at_ms == _bound_at_ms) return true;
  if (_udp) {
    Log::console(PSTR("UdpRx: WiFi reconnect, rebinding multicast"));
    teardownUdp();
  }
  if (!ensureUdp()) return false;
  _bound_at_ms = Wifi::connected_at_ms;
  _last_igmp_refresh_ms = now;
  do_refresh();
  return true;
}

// Silent rx recovery, one action a rung
void GeigerUdpRx::climbRecoveryLadder(uint32_t now) {
  uint32_t step_ms = (_recovery_steps < 5) ? 60000UL : 300000UL;
  if (_recovery_at_ms != 0 && (uint32_t)(now - _recovery_at_ms) < step_ms) return;
  _recovery_at_ms = now;
  switch (_recovery_steps) {
    case 0:
    case 1:
      do_refresh();
      break;
    case 3:
      Log::console(PSTR("UdpRx: silent %us, bouncing power save"),
                   (unsigned)((now - _last_packet_ms) / 1000u));
      Wifi::rx_sleep(2);
      _ps_bounce_until_ms = now + 5000u;
      break;
    case 4:
      Log::console(PSTR("UdpRx: silent %us, reassociating WiFi"),
                   (unsigned)((now - _last_packet_ms) / 1000u));
      WiFi.reconnect();
      break;
    default:
      teardownUdp();
      if (ensureUdp()) _bound_at_ms = Wifi::connected_at_ms;
      _rebind_count++;
      do_refresh();
      break;
  }
  _recovery_steps++;
  _last_igmp_refresh_ms = now;
}

void GeigerUdpRx::readDatagrams() {
  uint8_t buf[128];
  int sz;
  // a flood can't hold loop()
  uint8_t budget = 32;
  while (budget-- && (sz = _udp->parsePacket()) > 0) {
    if ((size_t)sz > sizeof(buf)) {
      while (_udp->available()) _udp->read();
      continue;
    }
    int got = _udp->read(buf, sz);
    if (got <= 0) continue;
    processDatagram(buf, (size_t)got);
  }
}

// not time ordered, swap with last
void GeigerUdpRx::fireDueGapfills() {
  uint32_t now_ms = fast_millis();
  uint8_t i = 0;
  while (i < _gapfill_count) {
    if ((int32_t)(now_ms - _gapfill_due_ms[i]) >= 0) {
      gcounter.dispatchReceiverBlip();
      _gapfill_count--;
      _gapfill_due_ms[i] = _gapfill_due_ms[_gapfill_count];
    } else {
      i++;
    }
  }
}

void GeigerUdpRx::loop() {
  uint32_t now = fast_millis();
  if (!ensureBound(now)) return;

  if (_ps_bounce_until_ms && (int32_t)(now - _ps_bounce_until_ms) >= 0) {
    _ps_bounce_until_ms = 0;
    Wifi::rx_sleep(_rx_mode);
  }

  if (_last_packet_ms > 0 && (uint32_t)(now - _last_packet_ms) > 30000UL) {
    climbRecoveryLadder(now);
    if (!_udp) return;
  } else {
    if (_recovery_steps) {
      Log::console(PSTR("UdpRx: rx resumed after rung %u"), _recovery_steps);
    }
    _recovery_steps = 0;
    _recovery_at_ms = 0;
    // keep the router subscription
    if ((uint32_t)(now - _last_igmp_refresh_ms) > 300000UL) {
      do_refresh();
      _last_igmp_refresh_ms = now;
    }
  }
  readDatagrams();
  if (_gapfill_count > 0) fireDueGapfills();
}

void GeigerUdpRx::secondTicker() {
  if (!_draining) return;
  float step = _drain_rate;
  if (step > _drain_pending) step = _drain_pending;
  _drain_pending -= step;
  _drain_partial += step;
  if (_drain_pending <= 0.0f) {
    _drain_pending = 0.0f;
    _drain_rate    = 0.0f;
    _draining      = false;
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

uint32_t GeigerUdpRx::quiet_s() const {
  return _last_packet_ms ? (uint32_t)(fast_millis() - _last_packet_ms) / 1000u : 0;
}

// igmpx = no netif
static bool m_quiet(EGMValue* v)   { v->u = s_udprx_instance->quiet_s(); return true; }
static bool m_rebind(EGMValue* v)  { v->u = s_udprx_instance->rebind_count(); return true; }
static bool m_igmp(EGMValue* v)    { v->u = s_udprx_instance->refresh_count(); return true; }
static bool m_igmpx(EGMValue* v)   { v->u = s_udprx_instance->refresh_noop_count(); return true; }

static bool m_loss(EGMValue* v) {
  if (s_udprx_instance->packets_accepted() == 0) return false;
  v->f = s_udprx_instance->loss_pct_x10() / 10.0f;
  return true;
}

static const char MP_LOSS[] PROGMEM = "geiger_udp_loss_percent";
static const char MH_LOSS[] PROGMEM = "Packet loss on the UDP feed";

static const EGMeasure UDP_ROWS[] PROGMEM = {
  EGM_ROW("quiet",    "", m_quiet,   EGM_U32, 0, EGMU_SEC,   EGM_OUT_JSON, 0),
  EGM_ROW("rebind",   "", m_rebind,  EGM_U32, 0, EGMU_COUNT, EGM_OUT_JSON, 0),
  EGM_ROW("igmp",     "", m_igmp,    EGM_U32, 0, EGMU_COUNT, EGM_OUT_JSON, 0),
  EGM_ROW("igmpx",    "", m_igmpx,   EGM_U32, 0, EGMU_COUNT, EGM_OUT_JSON, 0),
  EGM_PROW("loss",    "", m_loss,    EGM_F32, 1, EGMU_PCT,   EGM_OUT_JSON, 0, MP_LOSS, MH_LOSS, nullptr),
};

EGMeasureRows GeigerUdpRx::measurements() { return EGMeasureRows(UDP_ROWS); }

const char* GeigerUdpRx::locked_chipid() const {
  if (_mode == 1) return nullptr;   // sum mode
  if (_pinned_chipid[0]) return _pinned_chipid;
  if (_locked_chipid[0]) return _locked_chipid;
  return nullptr;
}

#endif // GEIGER_IS_UDPRX
