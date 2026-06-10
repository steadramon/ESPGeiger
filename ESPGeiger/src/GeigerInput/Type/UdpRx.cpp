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
#include "../../Util/FastMillis.h"

#if GEIGER_IS_UDPRX(GEIGER_TYPE)

#include "../../Counter/Counter.h"
#include "../../EnvSensor/EnvSensor.h"
#include "../../Logger/Logger.h"
#include "../../Prefs/EGPrefs.h"
#include "../../Util/DeviceInfo.h"
#include "../../Util/Wifi.h"
#include <EGHttpServer.h>
#include <WiFiUdp.h>
#include <string.h>
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
static constexpr const char TAG_II[]        = ",ii";
static constexpr const char TAG_FFF[]       = ",fff";

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
  bool ok;
  if (Wifi::is_multicast(_group)) {
#ifdef ESP8266
    ok = _udp->beginMulticast(Wifi::local_ip, _group, _port);
#else
    ok = _udp->beginMulticast(_group, _port);
#endif
    if (ok) Log::console(PSTR("UdpRx: joined %s:%u"),
                          EGPrefs::getString("input", "udprx_group"), _port);
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
  return &_producers[slot];
}

void GeigerUdpRx::processClick(const uint8_t* buf, size_t len, ProducerRecord* p, uint32_t now_ms) {
  // Tag string ",ii\0" = 4 bytes padded; args = 2 x int32 = 8 bytes.
  if (len < PATH_FULL_LEN + 4 + 8) return;
  if (buf[SUFFIX_OFFSET + 5] != '\0') return;
  if (memcmp(buf + PATH_FULL_LEN, TAG_II, 4) != 0) return;

  const uint8_t* args = buf + PATH_FULL_LEN + 4;
  uint32_t producer_counter = rd_i32(args);
  uint32_t producer_ts      = rd_i32(args + 4);

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
      // Producer ts_ms going way backwards also indicates a reboot, which
      // catches the case where it rebooted before counter reached 1024.
      uint32_t back = p->last_counter - producer_counter;
      bool ts_reset = (int32_t)(producer_ts - p->last_ts_ms) < -60000;
      if (back > 1024 || ts_reset) {
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
    gcounter.queueBlip();
    // Batch-path always: receiver-side timing would lie to s_min_pulse_us.
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
  _local_count    += credit;
  p->click_count  += credit;
  if (advance_anchor) {
    p->last_counter = producer_counter;
    p->last_ts_ms   = producer_ts;
  }
}

void GeigerUdpRx::processDatagram(uint8_t* buf, size_t len) {
  // buf[12] is the '/' between chipid and suffix - rejects anything that
  // isn't shaped like /espg/XXXXXX/ in a single byte.
  if (len < PATH_FULL_LEN + 4) return;
  if (buf[CHIPID_OFFSET + CHIPID_LEN] != '/') return;
  if (memcmp(buf, "/espg/", 6) != 0) return;
  // Silent self-loop reject. lwIP suppresses multicast loopback on both
  // ESP8266 and ESP32 today, but keeping the check is ~10 cycles of
  // insurance against future stack changes.
  if (memcmp(buf + CHIPID_OFFSET, DeviceInfo::chipid(), CHIPID_LEN) == 0) return;
  const char* src = (const char*)(buf + CHIPID_OFFSET);
  if (!acceptChipid(src)) return;

  uint32_t now_ms = fast_millis();
  ProducerRecord* p = findOrAllocProducer(src, now_ms);
  _last_packet_ms = now_ms;

  // /rad and /sys: path validated, producer keep-alive already bumped
  // by findOrAllocProducer + _last_packet_ms above. No payload decode
  // until /producers (or similar) lands.
  const uint8_t* s = buf + SUFFIX_OFFSET;
  if (s[0] == 'c') {
    if (s[1] != 'l' || s[2] != 'i' || s[3] != 'c' || s[4] != 'k') return;
    processClick(buf, len, p, now_ms);
  } else if (s[0] == 'e') {
    if (s[1] != 'n' || s[2] != 'v' || s[3] != '\0') return;
    processEnv(buf, len);
  }
}

void GeigerUdpRx::processEnv(const uint8_t* buf, size_t len) {
  // ",fff\0\0\0\0" typetag (8 bytes) + 3 x f32 (12 bytes).
  if (len < PATH_FULL_LEN + 8 + 12) return;
  if (memcmp(buf + PATH_FULL_LEN, TAG_FFF, 4) != 0) return;
  // Pin to the first producer that sends env. Stops sum/auto-mode receivers
  // from flapping between mismatched sources.
  static char s_env_chipid[CHIPID_LEN + 1] = {0};
  const char* src = (const char*)(buf + CHIPID_OFFSET);
  if (s_env_chipid[0] == '\0') {
    memcpy(s_env_chipid, src, CHIPID_LEN);
    s_env_chipid[CHIPID_LEN] = '\0';
  } else if (memcmp(s_env_chipid, src, CHIPID_LEN) != 0) {
    return;
  }
  const uint8_t* args = buf + PATH_FULL_LEN + 8;
  float t = rd_f32(args);
  float h = rd_f32(args + 4);
  float p = rd_f32(args + 8);
  envsensor.setRemote(t, h, p);
}

void GeigerUdpRx::loop() {
  // WiFi reconnect drops the IGMP membership; rebind the socket.
  if (_udp && Wifi::connected_at_ms != _bound_at_ms) {
    Log::console(PSTR("UdpRx: WiFi reconnect, rebinding multicast"));
    teardownUdp();
  }
  if (_udp && _last_packet_ms > 0 &&
      (fast_millis() - _last_packet_ms) > 30000UL) {
    teardownUdp();
    _last_packet_ms = 0;  // don't immediately re-tear if producer is genuinely offline
  }
  if (!ensureUdp()) return;
  _bound_at_ms = Wifi::connected_at_ms;
  uint8_t buf[128];
  int sz;
  while ((sz = _udp->parsePacket()) > 0) {
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

void GeigerUdpRx::appendJsonExtra(EGHttpResponse& res) {
  if (_packets_accepted == 0) return;
  uint16_t lx10 = loss_pct_x10();
  char buf[24];
  int n = snprintf_P(buf, sizeof(buf), PSTR(",\"loss\":%u.%u"),
                     (unsigned)(lx10 / 10), (unsigned)(lx10 % 10));
  if (n > 0) res.sendChunk(buf, (size_t)n);
}

const char* GeigerUdpRx::locked_chipid() const {
  if (_mode == 1) return nullptr;   // sum mode: caller uses producer_count
  if (_pinned_chipid[0]) return _pinned_chipid;
  if (_locked_chipid[0]) return _locked_chipid;
  return nullptr;
}

#endif // GEIGER_IS_UDPRX
