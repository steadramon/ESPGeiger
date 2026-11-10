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
#include "Corium/Measure/Measure.h"
#include "Corium/Module/Registry.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Prefs/Prefs.h"
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
#include "Corium/Util/Bytes.h"
#include <EGEscape.h>
#include <WiFiUdp.h>
#include <new>
#ifdef ESP8266
#include <ESP8266mDNS.h>
#else
#include <ESPmDNS.h>
#endif

CORIUM_PSTR(K_GROUP, "group");

#if GEIGER_IS_UDPRX(GEIGER_TYPE)
static constexpr uint16_t LOOP_MS = 1000;
#else
static constexpr uint16_t LOOP_MS = UDPBLIP_CLICK_MIN_INTERVAL_MS;
#endif
static constexpr uint8_t DRAIN_MAX = 8;   // a stats pass, the 8266 keeps ~5

UdpBlipModule& udpblip = UdpBlipModule::getInstance();
CORIUM_REGISTER_MODULE(udpblip)

CORIUM_PSTR(UB_O_MODE,  "Off|Telemetry only|Telemetry + per-click");
CORIUM_PSTR(UB_L_GROUP, "Group / peer IP");
CORIUM_PSTR(UB_H_GROUP, "239.x = multicast, else unicast peer.");


static const corium::Pref UDPBLIP_PREF_ITEMS[] PROGMEM = {
  {EG_K_MODE, EG_L_MOD,   nullptr,    "0",                    UB_O_MODE,  0, 2, 0,  corium::P_ENUM,   0},
  {K_GROUP,   UB_L_GROUP, UB_H_GROUP, UDPBLIP_DEFAULT_GROUP,  EG_P_IP,    0, 0, CORIUM_IPV4_MAX, corium::P_STRING, corium::P_ADVANCED},
  {EG_K_PORT, EG_L_PRT,   nullptr,    STR(UDPBLIP_DEFAULT_PORT), nullptr, 1, 65535, 0, corium::P_UINT, corium::P_ADVANCED},
};

static const corium::PrefGroup UDPBLIP_PREF_GROUP PROGMEM = {
  "udpblip", "Local Broadcast", 1,
  UDPBLIP_PREF_ITEMS,
  sizeof(UDPBLIP_PREF_ITEMS) / sizeof(UDPBLIP_PREF_ITEMS[0]),
  corium::P_CAT_OUTPUT, EG_K_MODE,
};

const corium::PrefGroup* UdpBlipModule::prefsGroup() { return &UDPBLIP_PREF_GROUP; }


static void udpblip_click(uint32_t) { udpblip.notifyClick(corium::fastMillis()); }

void UdpBlipModule::begin() {
  gcounter.on_click(udpblip_click);
  readPrefs();
  _last_clicks = gcounter.total_clicks;
  // jitter so a fleet doesn't send at once
  uint16_t jitter_s = UDPBLIP_STATS_JITTER_MS
                    ? (uint16_t)((GRNG::fast_uint32() % UDPBLIP_STATS_JITTER_MS) / 1000UL) : 0;
  _jitter_ms = jitter_s * 1000;
  _fail_count = 0;
  corium::Registry::on(corium::Wifi::TOPIC_CHANGED, this);
  snprintf_P(_path, sizeof(_path), PSTR("/espg/%s/"), corium::DeviceInfo::chipId());
  blip_click(_click_packet, sizeof(_click_packet), path(PSTR("click")));
  if (_mode > UB_OFF) {
    corium::Log::console(PSTR("UdpBlip: mode=%u group=%s port=%u"),
                 _mode,
                 corium::Prefs::getString("udpblip", K_GROUP),
                 _port);
  }
}

void UdpBlipModule::readPrefs() {
  _mode = (uint8_t)corium::Prefs::getUInt("udpblip", EG_K_MODE);
  const char* group = corium::Prefs::getString("udpblip", K_GROUP);
  if (!group || !group[0] || !_group.fromString(group)) {
    _group.fromString(UDPBLIP_DEFAULT_GROUP);
  }
  uint32_t p = corium::Prefs::getUInt("udpblip", EG_K_PORT);
  _port = (p > 0 && p < 65536) ? (uint16_t)p : UDPBLIP_DEFAULT_PORT;
  // gate retry pace, the loop sets its own due; off runs till mDNS is down
  corium::Registry::every(this, (_mode > UB_OFF || _mdns_port) ? LOOP_MS : 0);
  _wake_at = corium::fastMillis();
#if !GEIGER_IS_UDPRX(GEIGER_TYPE)
  // serial counts land with no click
  if (_mode == UB_CLICKS) corium::Registry::on(Counter::TOPIC_COUNTS, this);
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
  _udp = new (std::nothrow) WiFiUDP();
  if (!_udp) return false;
  // random, not lwIP's shared 60288
  if (_src_port == 0) {
    _src_port = (uint16_t)(49152u + (GRNG::fast_uint32() & 0x3FFFu));
  }
  if (_udp->begin(_src_port)) return true;
  teardown();
  _src_port = 0;   // maybe taken, another next time
  return false;
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

// both stacks refuse a second add, so a new port goes on the one up
static bool mdns_move(uint16_t port) {
#ifdef ESP8266
  (void)MDNS.removeService(nullptr, "osc", "udp");
#else
  // its remove is queued, an add straight after is still refused
  if (mdns_service_port_set("_osc", "_udp", port) == ESP_OK) return true;
#endif
  return MDNS.addService("osc", "udp", port);
}

static bool mdns_off() {
#ifdef ESP8266
  return MDNS.removeService(nullptr, "osc", "udp");
#else
  return mdns_service_remove("_osc", "_udp") == ESP_OK;
#endif
}

void UdpBlipModule::announce_mdns() {
  // after ArduinoOTA starts mDNS
  if (corium::Uptime::seconds() < 8) return;
  bool move = _mdns_port && _mdns_port != _port;
  bool ok = move ? mdns_move(_port) : MDNS.addService("osc", "udp", _port);
  if (ok) _mdns_port = _port;
  else if (move) return;   // the 8266 holds the old one in its probe, again next pass
  const char* fname = corium::DeviceInfo::friendlyName();
  if (fname && fname[0]) MDNS.addServiceTxt("osc", "udp", "fname", fname);
  MDNS.addServiceTxt("osc", "udp", "id", corium::DeviceInfo::chipId());
  MDNS.addServiceTxt("osc", "udp", "group", corium::Prefs::getString("udpblip", K_GROUP));  // no _P api
  _mdns_done = true;
}

void UdpBlipModule::onPrefsSaved() {
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
  corium::Log::console(PSTR("UdpBlip: prefs saved, mode=%u port=%u"), _mode, _port);
}

void UdpBlipModule::sendPacket(const uint8_t* buf, size_t len) {
  if (!_udp) return;
  if (!corium::Wifi::connected) return;
#ifdef ESP8266
  // esp8266 needs this for IGMP
  bool ok = corium::Wifi::isMulticast(_group)
              ? _udp->beginPacketMulticast(_group, _port, corium::Wifi::local_ip)
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
  _backoff.set(corium::fastMillis() + UDPBLIP_FAIL_COOLDOWN_MS);
  wake(_backoff.at);
}

void UdpBlipModule::emitClick(uint32_t count, uint32_t ts_ms) {
  wrBe32(&_click_packet[BLIP_CLICK_COUNT_AT], count);
  wrBe32(&_click_packet[BLIP_CLICK_MS_AT], ts_ms);
  sendPacket(_click_packet, sizeof(_click_packet));
}

static PGM_P rad_state() {
  if (corium::m_b(M_ALERT)) return PSTR("alert");
  if (corium::m_b(M_WARN)) return PSTR("warning");
  if (gcounter.counts_missing()) return PSTR("nocounts");
  return corium::m_b(M_WARM) ? PSTR("healthy") : PSTR("warming");
}

void UdpBlipModule::emitRad() {
  uint8_t buf[96];
  BlipRad r = { corium::m_f(M_CPM), corium::m_f(M_USV), rad_state(), corium::m_u(M_TC) };
  size_t n = blip_rad(buf, sizeof(buf), path(PSTR("rad")), r);
  if (n) sendPacket(buf, n);
}

void UdpBlipModule::emitHv() {
  uint8_t buf[64];
  // nan with no adc
  int32_t v;
  BlipHv h = { m_hv_volts(&v) ? (float)v : NAN, (float)corium::m_u(M_HV_TGT),
               corium::m_u(M_HV_DTY), corium::m_i(M_HV_TRM) };
  size_t n = blip_hv(buf, sizeof(buf), path(PSTR("hv")), h);
  if (n) sendPacket(buf, n);
}

void UdpBlipModule::emitEnv() {
  // never re-broadcast UDP values
  if (!envsensor.localPresent()) return;
  uint8_t buf[64];
  size_t n = blip_env(buf, sizeof(buf), path(PSTR("env")),
                      envsensor.tempC(), envsensor.humidity(), envsensor.pressure());
  if (n) sendPacket(buf, n);
}

void UdpBlipModule::emitSys() {
  uint8_t buf[64];
  BlipSys s = { (uint32_t)corium::DeviceInfo::uptime(), corium::Wifi::rssi,
                corium::TickProfile::lps, corium::TickProfile::tickMaxUs };
  size_t n = blip_sys(buf, sizeof(buf), path(PSTR("sys")), s);
  if (n) sendPacket(buf, n);
}

static_assert(BLIP_ID_LEN == CHIPID_LEN, "blip id is the chip id");

const char* UdpBlipModule::path(const char* suffix) {
  flashToRam(suffix, _path + BLIP_SUFFIX_AT, sizeof(_path) - BLIP_SUFFIX_AT);
  return _path;
}

// true = one waits for a token, woken then
bool UdpBlipModule::tryEmitClick(unsigned long now_ms) {
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  // never re-broadcast, feedback loop
  (void)now_ms;
  return false;
#else
  if (_mode != UB_CLICKS) return false;
  if (_fail_count >= UDPBLIP_FAIL_BACKOFF) return false;
  if (!_udp) return false;
  if (!corium::Wifi::connected) return false;
  uint32_t true_count = (uint32_t)gcounter.total_clicks + s_event_counter;
  if (true_count == _last_clicks) return false;
  if (!corium::otaInProgress && takeToken(now_ms, _last_token_ms, _tokens,
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
  corium::Registry::dueAt(this, at);
}

// sooner only, one call per token at most
void UdpBlipModule::wake(uint32_t at) {
  if (corium::reached(at, _wake_at)) return;
  due(at);
}

static inline void sooner(uint32_t& at, uint32_t t) {
  if (!corium::reached(t, at)) at = t;
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
    emitRad();
  } else if (_emit_phase == 1) {
    if (M_HAS_HV) emitHv();
  } else {
    _sys_phase = !_sys_phase;
    if (_sys_phase) {
      emitSys();
      emitEnv();
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
  _bound_at_ms = corium::Wifi::connectedAtMs;
  announce_mdns();
}

void UdpBlipModule::loop(unsigned long now) {
  if (_mode == UB_OFF) {
    // the 8266 refuses it in the probe after an add
    if (_mdns_port && mdns_off()) _mdns_port = 0;
    if (!_mdns_port) corium::Registry::every(this, 0);
    return;
  }
  uint16_t ev = corium::Registry::events(this);
  if ((ev & (1u << corium::Wifi::TOPIC_CHANGED)) && _udp && corium::Wifi::connectedAtMs != _bound_at_ms) {
    corium::Log::console(PSTR("UdpBlip: WiFi reconnect, rebinding"));
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
  if (corium::reached(now, _stats_at)) stats(now);
  schedule(now, tryEmitClick(now));
}

size_t UdpBlipModule::statusJson(char* buf, size_t cap, unsigned long now) {
  if (_mode == UB_OFF) return 0;
  // full length, like snprintf
  size_t pos = 0, need = 0;
  size_t n = snprintf_P(buf, cap, PSTR("\"udpblip\":{\"mode\":%u,\"group\":\""), _mode);
  need += n;
  advancePos(pos, n, cap);
  n = egesc_json(buf + pos, cap - pos, corium::Prefs::getString("udpblip", K_GROUP));
  need += n;
  advancePos(pos, n, cap);
  need += snprintf_P(buf + pos, cap - pos, PSTR("\",\"port\":%u,\"fails\":%u}"),
                     _port, _fail_count);
  return need;
}
