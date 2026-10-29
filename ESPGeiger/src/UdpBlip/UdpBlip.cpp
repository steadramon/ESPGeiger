/*
  UdpBlip.cpp - OSC over UDP multicast click + stats broadcaster

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
#include "UdpBlip.h"
#include "BlipPacket.h"
#include "App/AppKeys.h"
#include "App/MeasureIds.h"
#include "Counter/Counter.h"
#include "GeigerInput/GeigerInput.h"
#include "Corium/Measure/EGMeasure.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/Net/Wifi.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Util/TokenBucket.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/System/Uptime.h"
#include "GRNG/GRNG.h"
#include "Corium/System/TickProfile.h"
#include "EnvSensor/EnvSensor.h"
#include "Corium/Util/StringUtil.h"
#include <EGEscape.h>
#include <WiFiUdp.h>
#ifdef ESP8266
#include <ESP8266mDNS.h>
#else
#include <ESPmDNS.h>
#endif

EG_PSTR(K_GROUP, "group");

#if GEIGER_IS_UDPRX(GEIGER_TYPE)
static constexpr uint16_t LOOP_MS = 1000;
#else
static constexpr uint16_t LOOP_MS = UDPBLIP_CLICK_MIN_INTERVAL_MS;
#endif
static constexpr uint8_t DRAIN_MAX = 8;   // a stats pass, the 8266 keeps ~5

UdpBlipModule& udpblip = UdpBlipModule::getInstance();
EG_REGISTER_MODULE(udpblip)

EG_PSTR(UB_O_MODE,  "Off|Telemetry only|Telemetry + per-click");
EG_PSTR(UB_L_GROUP, "Group / peer IP");
EG_PSTR(UB_H_GROUP, "239.x = multicast, else unicast peer.");


static const EGPref UDPBLIP_PREF_ITEMS[] PROGMEM = {
  {EG_K_MODE, EG_L_MOD,   nullptr,    "0",                    UB_O_MODE,  0, 2, 0,  EGP_ENUM,   0},
  {K_GROUP,   UB_L_GROUP, UB_H_GROUP, UDPBLIP_DEFAULT_GROUP,  EG_P_IP,    0, 0, EG_IPV4_MAX, EGP_STRING, EGP_ADVANCED},
  {EG_K_PORT, EG_L_PRT,   nullptr,    STR(UDPBLIP_DEFAULT_PORT), nullptr, 1, 65535, 0, EGP_UINT, EGP_ADVANCED},
};

static const EGPrefGroup UDPBLIP_PREF_GROUP PROGMEM = {
  "udpblip", "Local Broadcast", 1,
  UDPBLIP_PREF_ITEMS,
  sizeof(UDPBLIP_PREF_ITEMS) / sizeof(UDPBLIP_PREF_ITEMS[0]),
  EGP_CAT_OUTPUT, EG_K_MODE,
};

const EGPrefGroup* UdpBlipModule::prefs_group() { return &UDPBLIP_PREF_GROUP; }


static void udpblip_click(uint32_t) { udpblip.notifyClick(fast_millis()); }

void UdpBlipModule::begin() {
  gcounter.on_click(udpblip_click);
  readPrefs();
  _last_clicks = gcounter.total_clicks;
  // jitter so a fleet doesn't send at once
  uint16_t jitter_s = UDPBLIP_STATS_JITTER_MS
                    ? (uint16_t)((GRNG::fast_uint32() % UDPBLIP_STATS_JITTER_MS) / 1000UL) : 0;
  _jitter_ms = jitter_s * 1000;
  _fail_count = 0;
  EGModuleRegistry::on(Wifi::TOPIC_CHANGED, this);
  snprintf_P(_path, sizeof(_path), PSTR("/espg/%s/"), DeviceInfo::chipid());
  blip_click(_click_packet, sizeof(_click_packet), path(PSTR("click")));
  if (_mode > 0) {
    Log::console(PSTR("UdpBlip: mode=%u group=%s port=%u"),
                 _mode,
                 EGPrefs::getString("udpblip", K_GROUP),
                 _port);
  }
}

void UdpBlipModule::readPrefs() {
  _mode = (uint8_t)EGPrefs::getUInt("udpblip", EG_K_MODE);
  const char* gstr = EGPrefs::getString("udpblip", K_GROUP);
  if (!gstr || !gstr[0] || !_group.fromString(gstr)) {
    _group.fromString(UDPBLIP_DEFAULT_GROUP);
  }
  uint32_t p = EGPrefs::getUInt("udpblip", EG_K_PORT);
  _port = (p > 0 && p < 65536) ? (uint16_t)p : UDPBLIP_DEFAULT_PORT;
  // gate retry pace, the loop sets its own due
  EGModuleRegistry::every(this, _mode > 0 ? LOOP_MS : 0);
  _wake_at = fast_millis();
#if !GEIGER_IS_UDPRX(GEIGER_TYPE)
  // serial counts land with no click
  if (_mode == 2) EGModuleRegistry::on(Counter::TOPIC_COUNTS, this);
#endif
}

void UdpBlipModule::teardown() {
  if (_udp) {
    _udp->stop();
    delete _udp;
    _udp = nullptr;
  }
  _mdns_done = false;
}

bool UdpBlipModule::ensureUdp() {
  if (_udp) return true;
  _udp = new WiFiUDP();
  if (!_udp) {
    Log::console(PSTR("UdpBlip: WiFiUDP alloc failed"));
    return false;
  }
  // random, not lwIP's shared 60288
  if (_src_port == 0) {
    _src_port = (uint16_t)(49152u + (GRNG::fast_uint32() & 0x3FFFu));
  }
  _udp->begin(_src_port);
  return true;
}

// send only, drop what lands so the stack doesn't hold it
void UdpBlipModule::drain() {
  if (!_udp) return;
  for (uint8_t i = 0; i < DRAIN_MAX && _udp->parsePacket() > 0; i++) {
#ifndef ESP8266
    _udp->flush();   // esp32 keeps one till flush, the 8266's flush sends
#endif
  }
}

void UdpBlipModule::announce_mdns() {
  if (_mdns_done || _mode == 0) return;
  // after ArduinoOTA starts mDNS
  if (Uptime::seconds() < 8) return;
  MDNS.addService("osc", "udp", _port);
  const char* fname = DeviceInfo::friendlyName();
  if (fname && fname[0]) MDNS.addServiceTxt("osc", "udp", "fname", fname);
  MDNS.addServiceTxt("osc", "udp", "id", DeviceInfo::chipid());
  MDNS.addServiceTxt("osc", "udp", "group", EGPrefs::getString("udpblip", K_GROUP));
  _mdns_done = true;
}

void UdpBlipModule::on_prefs_saved() {
  uint8_t old_mode = _mode;
  uint16_t old_port = _port;
  IPAddress old_group = _group;
  readPrefs();
  bool changed = (old_mode != _mode) || (old_port != _port) || !(old_group == _group);
  if (!changed) return;
  teardown();
  _fail_count = 0;
  // no burst of stale clicks
  _last_clicks = (uint32_t)gcounter.total_clicks;
  Log::console(PSTR("UdpBlip: prefs saved, mode=%u port=%u"), _mode, _port);
}

void UdpBlipModule::sendPacket(const uint8_t* buf, size_t len) {
  if (!_udp) return;
  if (!Wifi::connected) return;
#ifdef ESP8266
  // esp8266 needs this for IGMP
  bool ok = Wifi::is_multicast(_group)
              ? _udp->beginPacketMulticast(_group, _port, Wifi::local_ip)
              : _udp->beginPacket(_group, _port);
#else
  bool ok = _udp->beginPacket(_group, _port);
#endif
  if (ok) {
    _udp->write(buf, len);
    ok = _udp->endPacket() != 0;
  }
  if (ok) {
    _fail_count = 0;
    return;
  }
  // the last one starts the cooldown
  if (++_fail_count != UDPBLIP_FAIL_BACKOFF) return;
  _backoff.set(fast_millis() + UDPBLIP_FAIL_COOLDOWN_MS);
  wake(_backoff.at);
}

void UdpBlipModule::emitClick(uint32_t counter, uint32_t ts_ms) {
  uint32_t cnt_be = __builtin_bswap32(counter);
  uint32_t ts_be  = __builtin_bswap32(ts_ms);
  memcpy(&_click_packet[BLIP_CLICK_COUNT_AT], &cnt_be, 4);
  memcpy(&_click_packet[BLIP_CLICK_MS_AT],    &ts_be,  4);
  sendPacket(_click_packet, sizeof(_click_packet));
}

void UdpBlipModule::emitRad(uint32_t now) {
  uint8_t buf[96];
  const char* state =
    egm_b(M_ALERT) ? PSTR("alert") :
    egm_b(M_WARN)  ? PSTR("warning") :
    gcounter.counts_missing() ? PSTR("nocounts") :
    egm_b(M_WARM)  ? PSTR("healthy") : PSTR("warming");
  BlipRad r = { egm_f(M_CPM), egm_f(M_USV), state, egm_u(M_TC) };
  size_t n = blip_rad(buf, sizeof(buf), path(PSTR("rad")), r);
  if (n) sendPacket(buf, n);
  (void)now;
}

void UdpBlipModule::emitHv(uint32_t now) {
  uint8_t buf[64];
  // nan with no adc
  EGMeasureIter v(0xFF, false, M_HV);
  BlipHv h = { v.next() ? (float)v.value().i : NAN, (float)egm_u(M_HV_TGT),
               egm_u(M_HV_DTY), egm_i(M_HV_TRM) };
  size_t n = blip_hv(buf, sizeof(buf), path(PSTR("hv")), h);
  if (n) sendPacket(buf, n);
  (void)now;
}

void UdpBlipModule::emitEnv(uint32_t now) {
  // never re-broadcast UDP values
  if (!envsensor.localPresent()) return;
  uint8_t buf[64];
  size_t n = blip_env(buf, sizeof(buf), path(PSTR("env")),
                      envsensor.tempC(), envsensor.humidity(), envsensor.pressure());
  if (n) sendPacket(buf, n);
  (void)now;
}

void UdpBlipModule::emitSys(uint32_t now) {
  uint8_t buf[64];
  BlipSys s = { (uint32_t)DeviceInfo::uptime(), Wifi::rssi,
                TickProfile::lps, TickProfile::tick_max_us };
  size_t n = blip_sys(buf, sizeof(buf), path(PSTR("sys")), s);
  if (n) sendPacket(buf, n);
  (void)now;
}

static_assert(BLIP_ID_LEN == CHIPID_LEN, "blip id is the chip id");

const char* UdpBlipModule::path(const char* suffix) {
  flash_to_ram(suffix, _path + BLIP_SUFFIX_AT, sizeof(_path) - BLIP_SUFFIX_AT);
  return _path;
}

// true = one waits for a token, woken then
bool UdpBlipModule::tryEmitClick(unsigned long now_ms) {
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  // never re-broadcast, feedback loop
  (void)now_ms;
  return false;
#else
  if (_mode != 2) return false;
  if (_fail_count >= UDPBLIP_FAIL_BACKOFF) return false;
  if (!_udp) return false;
  if (!Wifi::connected) return false;
  uint32_t true_count = (uint32_t)gcounter.total_clicks + s_event_counter;
  if (true_count == _last_clicks) return false;
  if (!ota_in_progress && take_token(now_ms, _last_token_ms, _burst_tokens,
                                     UDPBLIP_CLICK_MIN_INTERVAL_MS, UDPBLIP_CLICK_BURST_TOKENS)) {
    _last_clicks = true_count;
    emitClick(true_count, (uint32_t)now_ms);
    return false;
  }
  wake(_last_token_ms + UDPBLIP_CLICK_MIN_INTERVAL_MS);
  return true;
#endif
}

void UdpBlipModule::due(uint32_t at) {
  _wake_at = at;
  EGModuleRegistry::due_at(this, at);
}

// sooner only, one call per token at most
void UdpBlipModule::wake(uint32_t at) {
  if (reached(at, _wake_at)) return;
  due(at);
}

static inline void sooner(uint32_t& at, uint32_t t) {
  if (!reached(t, at)) at = t;
}

// stats, the cooldown, a held click or the bind retry
void UdpBlipModule::schedule(uint32_t now, bool held) {
  uint32_t at = _stats_at;
  if (_fail_count >= UDPBLIP_FAIL_BACKOFF) {
    at = _backoff.at;
  } else {
    if (!_udp || !_mdns_done) sooner(at, now + LOOP_MS);
    if (held) sooner(at, _last_token_ms + UDPBLIP_CLICK_MIN_INTERVAL_MS);
  }
  due(at);
}

// rad, hv a second on, sys + env every other cycle
void UdpBlipModule::stats(uint32_t now) {
  drain();
  if (_emit_phase == 0) {
    emitRad(now);
  } else if (_emit_phase == 1) {
    if (M_HAS_HV) emitHv(now);
  } else {
    _sys_phase = !_sys_phase;
    if (_sys_phase) {
      emitSys(now);
      emitEnv(now);
    }
  }
  _stats_at = now + (_emit_phase == 2 ? UDPBLIP_STATS_INTERVAL_MS : 1000);
  _emit_phase = (_emit_phase + 1) % 3;
}

// a new socket starts the cycle, as at boot
void UdpBlipModule::bind(uint32_t now) {
  if (!_udp) {
    if (!ensureUdp()) return;
    _stats_at = now + UDPBLIP_STATS_INTERVAL_MS - 1000 - _jitter_ms;   // bind second counts
    _emit_phase = 0;
    _sys_phase = false;
  }
  _bound_at_ms = Wifi::connected_at_ms;
  announce_mdns();
}

void UdpBlipModule::loop(unsigned long now) {
  if (_mode == 0) return;
  uint16_t ev = EGModuleRegistry::events(this);
  if ((ev & (1u << Wifi::TOPIC_CHANGED)) && _udp && Wifi::connected_at_ms != _bound_at_ms) {
    Log::console(PSTR("UdpBlip: WiFi reconnect, rebinding"));
    teardown();
  }
  if (_fail_count >= UDPBLIP_FAIL_BACKOFF) {
    if (!_backoff.due(now)) {
      due(_backoff.at);
      return;
    }
    _backoff.clear();
    _fail_count = 0;
    teardown();
  }
  if (!_udp || !_mdns_done) bind(now);
  if (reached(now, _stats_at)) stats(now);
  schedule(now, tryEmitClick(now));
}

size_t UdpBlipModule::status_json(char* buf, size_t cap, unsigned long now) {
  if (_mode == 0) return 0;
  // full length, like snprintf
  size_t pos = 0, need = 0;
  size_t n = snprintf_P(buf, cap, PSTR("\"udpblip\":{\"mode\":%u,\"group\":\""), _mode);
  need += n;
  advance_pos(pos, n, cap);
  n = egesc_json(buf + pos, cap - pos, EGPrefs::getString("udpblip", K_GROUP));
  need += n;
  advance_pos(pos, n, cap);
  need += snprintf_P(buf + pos, cap - pos, PSTR("\",\"port\":%u,\"fails\":%u}"),
                     _port, _fail_count);
  return need;
}
