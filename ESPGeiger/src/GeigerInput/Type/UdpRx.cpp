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
#include "App/Units.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/Prefs/PrefPattern.h"
#include "GeigerPrefs/GeigerKeys.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/Uptime.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Util/Bytes.h"
#include "Corium/Util/MathUtil.h"
#include <WiFiUdp.h>
#include <new>
#include <string.h>
#include <ctype.h>

static GeigerUdpRx* s_rx = nullptr;

CORIUM_PSTR(ID_PAT, UDPRX_CHIPID_PAT);

static constexpr const char PATH_PREFIX[]  = "/espg/";
static constexpr const char TAG_II[]        = ",ii";
static constexpr const char TAG_FFF[]       = ",fff";
// "env" includes its NUL
static constexpr const char SUFFIX_CLICK[]  = "click";
static constexpr const char SUFFIX_ENV[]    = "env";
// quiet is over QUIET_MS
static constexpr uint32_t HEARD_MS = UDPRX_QUIET_MS + 1;

static uint32_t up_s() { return (uint32_t)corium::Uptime::seconds(); }

static inline float rd_f32(const uint8_t* p) {
  uint32_t bits = rdBe32(p);
  float f;
  memcpy(&f, &bits, sizeof(f));
  return f;
}

// inf reads as missing
static float or_nan(float v) { return isFinite(v) ? v : NAN; }

extern "C" void udprx_notify_prefs_saved() {
  if (s_rx) s_rx->onPrefsSaved();
}

GeigerUdpRx::GeigerUdpRx() { s_rx = this; }
GeigerUdpRx::~GeigerUdpRx() { s_rx = nullptr; teardownUdp(); }

void GeigerUdpRx::begin() {
  _start_at = corium::fastMillis();
  _poll_at = _start_at + UDPRX_POLL_MS;
  _polls = true;
  readPrefs();
  _collected = _local_count;
  corium::Log::console(PSTR("UdpRx: mode=%u group=%s port=%u rxmode=%u"),
               _mode, corium::Prefs::getString("counter", EG_K_UDPRX_GROUP),
               _port, _rx_mode);
}

void GeigerUdpRx::readPrefs() {
  _mode = (uint8_t)corium::Prefs::getUInt("counter", EG_K_UDPRX_MODE);
  if (_mode > RX_SUM) _mode = RX_ONE;   // legacy 2 = auto
  const char* id = corium::Prefs::getString("counter", EG_K_UDPRX_CHIPID);
  size_t n = strlen(id);
  if (n && n != BLIP_ID_LEN) {   // a short pin would match nothing
    corium::Log::console(PSTR("UdpRx: Pin %s is not %u chars, ignored"), id, (unsigned)BLIP_ID_LEN);
    n = 0;
  }
  // ids are sent lower case
  for (size_t i = 0; i < n; i++) _pinned_chipid[i] = tolower((unsigned char)id[i]);
  _pinned_chipid[n] = '\0';

  const char* g = corium::Prefs::getString("counter", EG_K_UDPRX_GROUP);
  if (!g || !g[0] || !_group.fromString(g)) {
    _group.fromString(UDPRX_DEFAULT_GROUP);
  }
  uint32_t p = corium::Prefs::getUInt("counter", EG_K_UDPRX_PORT);
  _port = (p > 0 && p < 65536) ? (uint16_t)p : UDPRX_DEFAULT_PORT;
  uint32_t rm = corium::Prefs::getUInt("counter", EG_K_UDPRX_RXMODE);
  _rx_mode = (rm <= 2) ? (uint8_t)rm : 1;
}

void GeigerUdpRx::onPrefsSaved() {
  uint8_t  old_mode = _mode;
  uint16_t old_port = _port;
  uint8_t  old_rx_mode = _rx_mode;
  IPAddress old_group = _group;
  char old_pin[BLIP_ID_LEN + 1];
  memcpy(old_pin, _pinned_chipid, sizeof(old_pin));
  readPrefs();
  bool net_changed = (old_port != _port) || !(old_group == _group);
  if (net_changed) {
    teardownUdp();
  }
  if (old_mode != _mode || strcmp(old_pin, _pinned_chipid) != 0) {
    unlatch();
  }
  if (old_rx_mode != _rx_mode && _udp) {
    corium::Wifi::rxSleep(_rx_mode);
  }
  corium::Log::console(PSTR("UdpRx: Prefs saved, mode=%u port=%u rxmode=%u"),
               _mode, _port, _rx_mode);
}

void GeigerUdpRx::teardownUdp() {
  if (_udp) {
    _udp->stop();
    delete _udp;
    _udp = nullptr;
  }
  _joined.end();
}

bool GeigerUdpRx::ensureUdp() {
  if (_udp) return true;
  if (!corium::Wifi::connected) return false;
  _udp = new (std::nothrow) WiFiUDP();
  if (!_udp) return false;
  corium::Wifi::rxSleep(_rx_mode);
  bool ok;
  if (corium::Wifi::isMulticast(_group)) {
    ok = _joined.begin(*_udp, _group, _port);
    if (ok) corium::Log::console(PSTR("UdpRx: Joined %s:%u"),
                          corium::Prefs::getString("counter", EG_K_UDPRX_GROUP), _port);
  } else {
    ok = _udp->begin(_port);
    if (ok) corium::Log::console(PSTR("UdpRx: Listening unicast on :%u"), _port);
  }
  if (!ok) {
    teardownUdp();
    return false;
  }
  return true;
}

// next click latches again
void GeigerUdpRx::unlatch() {
  _locked_chipid[0] = '\0';
  _env_chipid[0] = '\0';
}

bool GeigerUdpRx::acceptChipid(const char* src_chipid, bool click) {
  if (summing()) return true;
  // pinned chipid, else first click heard
  if (_pinned_chipid[0]) {
    return memcmp(_pinned_chipid, src_chipid, BLIP_ID_LEN) == 0;
  }
  if (_locked_chipid[0] == '\0') {
    if (!click) return false;
    memcpy(_locked_chipid, src_chipid, BLIP_ID_LEN);
    _locked_chipid[BLIP_ID_LEN] = '\0';
    corium::Log::console(PSTR("UdpRx: Locked to %s"), _locked_chipid);
    return true;
  }
  return memcmp(_locked_chipid, src_chipid, BLIP_ID_LEN) == 0;
}

GeigerUdpRx::ProducerRecord* GeigerUdpRx::producer(const char* chipid,
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

// a flood can't log every packet
bool GeigerUdpRx::logOk() {
  uint32_t now = corium::fastMillis();
  if (now - _log_at >= 60000UL) {
    _log_at = now;
    _log_left = UDPRX_LOG_MAX;
  }
  if (!_log_left) return false;
  _log_left--;
  return true;
}

// <= 1024 now, to 65535 drained, more = resync
uint32_t GeigerUdpRx::creditForwardGap(const ProducerRecord* p, uint32_t count,
                                       uint32_t ts_ms) {
  uint32_t gap = count - p->last_counter - 1;
  if (gap <= 1024) return gap;
  if (gap <= UDPRX_DRAIN_MAX) {
    uint32_t dt_ms = ts_ms - p->last_ts_ms;
    if (dt_ms > 100 && dt_ms < 60000) {
      _drain_pending += (float)gap;
      if (_drain_pending > UDPRX_DRAIN_MAX) _drain_pending = UDPRX_DRAIN_MAX;
      float r = (float)gap * 1000.0f / (float)dt_ms;
      if (r > _drain_rate) _drain_rate = r;
      _draining = true;
    } else {
      _resync_count++;
      if (logOk()) corium::Log::console(PSTR("UdpRx: %s drain rejected, gap=%u dt=%ums"),
                                p->chipid, (unsigned)gap, (unsigned)dt_ms);
    }
    return 0;
  }
  _resync_count++;
  if (logOk()) corium::Log::console(PSTR("UdpRx: %s resync, gap=%u (counter %u -> %u)"),
                            p->chipid, (unsigned)gap,
                            (unsigned)p->last_counter, (unsigned)count);
  return 0;
}

// small = reorder, big or ts back = producer reboot
bool GeigerUdpRx::producerRestarted(const ProducerRecord* p, uint32_t count,
                                    uint32_t ts_ms) {
  uint32_t back = p->last_counter - count;
  bool ts_reset = (int32_t)(ts_ms - p->last_ts_ms) < -60000;
  if (back > 1024 || ts_reset) {
    _resync_count++;
    if (logOk()) corium::Log::console(PSTR("UdpRx: %s counter reset (%u -> %u)"),
                              p->chipid,
                              (unsigned)p->last_counter, (unsigned)count);
    return true;
  }
  return false;
}

// Poisson spread over <= 50 ms
void GeigerUdpRx::scheduleGapfill(uint32_t gap_credit, uint32_t window_ms, uint32_t now_ms) {
  if (window_ms == 0 || window_ms > 50) window_ms = 50;
  uint32_t r = (uint32_t)micros();
  for (uint32_t i = 0; i < gap_credit; i++) {
    if (_gapfill_count >= UDPRX_GAPFILL_QUEUE) break;
    r = xorshift32(r);
    _gapfill_due_ms[_gapfill_count++] = now_ms + (r % window_ms) + 1;
  }
}

void GeigerUdpRx::creditClicks(uint32_t credit, uint32_t gap_credit, const ProducerRecord* p,
                               uint32_t ts_ms, uint32_t now_ms) {
  _packets_accepted++;
  _last_click_ms  = now_ms;
  _gap_filled    += gap_credit;
  gcounter.dispatchReceiverBlip();
  if (gap_credit > 0) scheduleGapfill(gap_credit, ts_ms - p->last_ts_ms, now_ms);
  // batch, receiver timing would lie
  uint32_t now_us = (uint32_t)micros();
  if (credit == 1) {
    Counter::on_pulse_batch(1, now_us, 0);
  } else {
    uint32_t dt_ms = ts_ms - p->last_ts_ms;
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

  uint32_t count = rdBe32(buf + BLIP_CLICK_COUNT_AT);
  uint32_t ts_ms      = rdBe32(buf + BLIP_CLICK_MS_AT);

  uint32_t credit = 1;
  uint32_t gap_credit = 0;
  bool     advance_anchor = true;
  if (p->click_count > 0) {
    if (count == p->last_counter) {
      credit = 0;   // dup
    } else if (count > p->last_counter) {
      gap_credit = creditForwardGap(p, count, ts_ms);
      credit += gap_credit;
    } else if (!producerRestarted(p, count, ts_ms)) {
      credit = 0;
      advance_anchor = false;
    }
  }
  if (credit > 0) creditClicks(credit, gap_credit, p, ts_ms, now_ms);
  _local_count    += credit;
  p->click_count  += credit;
  if (advance_anchor) {
    p->last_counter = count;
    p->last_ts_ms   = ts_ms;
  }
}

void GeigerUdpRx::processDatagram(uint8_t* buf, size_t len) {
  if (len < BLIP_ARGS_AT + 4) return;
  if (buf[BLIP_ID_AT + BLIP_ID_LEN] != '/') return;
  if (memcmp(buf, PATH_PREFIX, BLIP_ID_AT) != 0) return;
  char src[BLIP_ID_LEN + 1];
  memcpy(src, buf + BLIP_ID_AT, BLIP_ID_LEN);
  src[BLIP_ID_LEN] = '\0';
  // "" fits the pattern
  if (!src[0] || prefPattern(ID_PAT, src) != 1) return;
  // own packets
  if (memcmp(src, corium::DeviceInfo::chipId(), BLIP_ID_LEN) == 0) return;
  const uint8_t* suffix = buf + BLIP_SUFFIX_AT;
  bool click = memcmp(suffix, SUFFIX_CLICK, 5) == 0;
  if (!acceptChipid(src, click)) return;

  uint32_t now_ms = corium::fastMillis();
  ProducerRecord* p = producer(src, now_ms);
  _last_packet_ms = now_ms;
  _last_packet_s = up_s();
  _any_packet = true;
  _heard.mark(now_ms);

  // other paths only keep the producer alive
  if (click) {
    processClick(buf, len, p, now_ms);
  } else if (memcmp(suffix, SUFFIX_ENV, 4) == 0) {
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
  for (uint8_t i = 0; i < 3; i++) _env[i] = or_nan(rd_f32(args + 4 * i));
  _env_at.mark(corium::fastMillis());
}

bool GeigerUdpRx::env(uint8_t i, float* v) {
  if (!_env_at.fresh(corium::fastMillis(), UDPRX_ENV_TTL_MS)) return false;
  *v = _env[i];
  return true;
}

void GeigerUdpRx::do_refresh() {
  if (!corium::IgmpRefresh::REFRESHES) return;   // igmpx is a fault, not the 8266's no-op
  if (corium::IgmpRefresh::refresh()) _refresh_count++;
  else                        _refresh_noop_count++;
}

// rebind after a wifi reconnect, igmp is lost
bool GeigerUdpRx::ensureBound(uint32_t now) {
  if (_udp && corium::Wifi::connectedAtMs == _bound_at_ms) return true;
  if (_udp) {
    corium::Log::console(PSTR("UdpRx: WiFi reconnect, rebinding multicast"));
    teardownUdp();
  }
  if (!ensureUdp()) return false;
  _bound_at_ms = corium::Wifi::connectedAtMs;
  _igmp_ms = now;
  do_refresh();
  return true;
}

// Silent rx recovery, one action a rung
void GeigerUdpRx::recover(uint32_t now) {
  uint32_t step_ms = (_recovery_steps < 5) ? 60000UL : 300000UL;
  if (corium::fresh(now, _recovery_at_ms, step_ms)) return;
  _recovery_at_ms = now;
  switch (_recovery_steps) {
    case 0:
      unlatch();   // a pin stays
      do_refresh();
      break;
    case 1:
      do_refresh();
      break;
    case 3:
      corium::Log::console(PSTR("UdpRx: Silent %us, bouncing power save"), (unsigned)quiet_s());
      corium::Wifi::rxSleep(2);
      _ps_bounce.set(now + 5000u);
      break;
    case 4:
      corium::Log::console(PSTR("UdpRx: Silent %us, reassociating WiFi"), (unsigned)quiet_s());
      corium::Wifi::reconnect();
      break;
    case 2:
    default:
      teardownUdp();
      if (ensureUdp()) _bound_at_ms = corium::Wifi::connectedAtMs;
      _rebind_count++;
      do_refresh();
      break;
  }
  _recovery_steps++;
  _igmp_ms = now;
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
  uint32_t now_ms = corium::fastMillis();
  uint8_t i = 0;
  while (i < _gapfill_count) {
    if (corium::reached(now_ms, _gapfill_due_ms[i])) {
      gcounter.dispatchReceiverBlip();
      _gapfill_count--;
      _gapfill_due_ms[i] = _gapfill_due_ms[_gapfill_count];
    } else {
      i++;
    }
  }
}

void GeigerUdpRx::loop() {
  uint32_t now = corium::fastMillis();
  _poll_at = now + UDPRX_POLL_MS;
  if (!ensureBound(now)) return;

  if (_ps_bounce.due(now)) {
    _ps_bounce.clear();
    corium::Wifi::rxSleep(_rx_mode);
  }

  if (_any_packet && !_heard.fresh(now, HEARD_MS)) {
    recover(now);
    if (!_udp) return;
  } else {
    if (_recovery_steps) {
      corium::Log::console(PSTR("UdpRx: RX resumed after rung %u"), (unsigned)_recovery_steps);
    }
    _recovery_steps = 0;
    _recovery_at_ms = 0;
    // keep the router subscription
    if ((uint32_t)(now - _igmp_ms) > 300000UL) {
      do_refresh();
      _igmp_ms = now;
    }
  }
  readDatagrams();
  if (_gapfill_count > 0) fireDueGapfills();
}

void GeigerUdpRx::secondTicker() {
  uint32_t now = corium::fastMillis();
  // stale stays stale over the wrap
  _env_at.fresh(now, UDPRX_ENV_TTL_MS);
  _heard.fresh(now, HEARD_MS);
  if (corium::dueSteps(now, _start_at, UDPRX_LOSS_STEP_MS)) {
    _starts[_oldest] = {_local_count, _gap_filled};
    if (++_oldest == UDPRX_LOSS_STEPS) _oldest = 0;
  }
  if (!_draining) return;
  // a big gap goes in at the sender's rate
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
  uint32_t count = _local_count;
  int delta = (int)(count - _collected);
  _collected = count;
  return delta;
}

bool GeigerUdpRx::isHealthy() const {
  return _producers_seen > 0;
}

uint32_t GeigerUdpRx::quiet_s() const {
  return _any_packet ? up_s() - _last_packet_s : 0;
}

bool GeigerUdpRx::heard() const {
  return corium::fresh(corium::fastMillis(), _heard.at, HEARD_MS);
}

// since the oldest start, counts wrap safe
bool GeigerUdpRx::loss_pct_x10(uint16_t* v) const {
  const Start& s = _starts[_oldest];
  uint32_t count = _local_count - s.count;
  if (!count) return false;
  *v = perMille(_gap_filled - s.lost, count);
  return true;
}

// igmpx = no netif
static bool m_quiet(corium::MValue* v)   { v->u = s_rx->quiet_s(); return true; }
static bool m_rebind(corium::MValue* v)  { v->u = s_rx->rebind_count(); return true; }
static bool m_igmp(corium::MValue* v)    { v->u = s_rx->refresh_count(); return true; }
static bool m_igmpx(corium::MValue* v)   { v->u = s_rx->refresh_noop_count(); return true; }

static bool m_loss(corium::MValue* v) {
  uint16_t x10;
  if (!s_rx->loss_pct_x10(&x10)) return false;
  v->f = x10 / 10.0f;
  return true;
}

static bool m_rx_t(corium::MValue* v) { return s_rx->env(0, &v->f); }
static bool m_rx_h(corium::MValue* v) { return s_rx->env(1, &v->f); }
static bool m_rx_p(corium::MValue* v) { return s_rx->env(2, &v->f); }

static const char MP_LOSS[] PROGMEM = "geiger_udp_loss_percent";
static const char MH_LOSS[] PROGMEM = "Packet loss on the UDP feed";

static const corium::Measure UDP_ROWS[] PROGMEM = {
  CORIUM_M_ROW("quiet",    "", m_quiet,   corium::M_U32, 0, corium::MU_SEC,   corium::M_OUT_JSON, 0),
  CORIUM_M_ROW("rebind",   "", m_rebind,  corium::M_U32, 0, EGMU_COUNT, corium::M_OUT_JSON, 0),
  CORIUM_M_ROW("igmp",     "", m_igmp,    corium::M_U32, 0, EGMU_COUNT, corium::M_OUT_JSON, 0),
  CORIUM_M_ROW("igmpx",    "", m_igmpx,   corium::M_U32, 0, EGMU_COUNT, corium::M_OUT_JSON, 0),
  CORIUM_M_PROW("loss",    "", m_loss,    corium::M_F32, 1, corium::MU_PCT,   corium::M_OUT_JSON, 0, MP_LOSS, MH_LOSS, nullptr),
  // the env module shows them
  CORIUM_M_ROW("rx_t",     "", m_rx_t,    corium::M_F32, 2, corium::MU_CELSIUS, corium::M_OUT_ID, 0),
  CORIUM_M_ROW("rx_h",     "", m_rx_h,    corium::M_F32, 2, corium::MU_PCT,   corium::M_OUT_ID, 0),
  CORIUM_M_ROW("rx_p",     "", m_rx_p,    corium::M_F32, 2, corium::MU_HPA,   corium::M_OUT_ID, 0),
};

corium::MeasureRows GeigerUdpRx::measurements() { return corium::MeasureRows(UDP_ROWS); }

const char* GeigerUdpRx::locked_chipid() const {
  if (summing()) return nullptr;
  if (_pinned_chipid[0]) return _pinned_chipid;
  if (_locked_chipid[0]) return _locked_chipid;
  return nullptr;
}

#endif // GEIGER_IS_UDPRX
