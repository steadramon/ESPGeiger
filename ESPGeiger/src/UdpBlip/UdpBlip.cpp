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
#include "../Counter/Counter.h"
#include "../GeigerInput/GeigerInput.h"
#include "../Corium/Module/EGModuleRegistry.h"
#include "../Corium/Logger/Logger.h"
#include "../Corium/Prefs/EGPrefs.h"
#include "../Corium/System/DeviceInfo.h"
#include "../Corium/Net/Wifi.h"
#include "../Corium/System/FastMillis.h"
#include "../Corium/System/Lifecycle.h"
#include "../GRNG/GRNG.h"
#include "../Corium/System/TickProfile.h"
#include "../EnvSensor/EnvSensor.h"
#include "../Corium/Util/StringUtil.h"
#include <EGEscape.h>
#ifdef ESPG_HV_ADC
#include "../HV/HV.h"
#endif
#include <WiFiUdp.h>
#ifdef ESP8266
#include <ESP8266mDNS.h>
#else
#include <ESPmDNS.h>
#endif

EG_PSTR(K_GROUP, "group");

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


void UdpBlipModule::begin() {
  readPrefs();
  _last_clicks = gcounter.total_clicks;
  // jitter so a fleet doesn't send at once
  uint16_t jitter_s = UDPBLIP_STATS_JITTER_MS
                    ? (uint16_t)((GRNG::fast_uint32() % UDPBLIP_STATS_JITTER_MS) / 1000UL) : 0;
  _stats_ticks = jitter_s;
  _backoff_ticks = 0;
  _fail_count = 0;
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
  EGModuleRegistry::set_tick_enabled(this, _mode > 0);
  EGModuleRegistry::set_loop_interval(this, _mode > 0 ? (int32_t)loop_interval_ms() : -1);
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

void UdpBlipModule::announce_mdns() {
  if (_mdns_done || _mode == 0) return;
  // after ArduinoOTA starts mDNS
  if (fast_millis() < 8000) return;
  MDNS.addService("osc", "udp", _port);  // NOLINT(dram)
  const char* fname = DeviceInfo::friendlyName();
  if (fname && fname[0]) MDNS.addServiceTxt("osc", "udp", "fname", fname);  // NOLINT(dram)
  MDNS.addServiceTxt("osc", "udp", "id", DeviceInfo::chipid());  // NOLINT(dram)
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
  _stats_ticks = 0;
  Log::console(PSTR("UdpBlip: prefs saved, mode=%u port=%u"), _mode, _port);
}

bool UdpBlipModule::sendPacket(const uint8_t* buf, size_t len) {
  if (!_udp) return false;
  if (!Wifi::connected) return false;
#ifdef ESP8266
  // esp8266 needs this for IGMP
  bool ok = Wifi::is_multicast(_group)
              ? _udp->beginPacketMulticast(_group, _port, Wifi::local_ip)
              : _udp->beginPacket(_group, _port);
#else
  bool ok = _udp->beginPacket(_group, _port);
#endif
  if (!ok) {
    _fail_count++;
    return false;
  }
  _udp->write(buf, len);
  if (_udp->endPacket() == 0) {
    _fail_count++;
    return false;
  }
  _fail_count = 0;
  return true;
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
    gcounter.is_alert()   ? PSTR("alert") :
    gcounter.is_warning() ? PSTR("warning") :
    gcounter.is_warm()    ? PSTR("healthy") : PSTR("warming");
  BlipRad r = { gcounter.get_cpmf(), gcounter.get_usv(), state, (uint32_t)gcounter.total_clicks };
  size_t n = blip_rad(buf, sizeof(buf), path(PSTR("rad")), r);
  if (n) sendPacket(buf, n);
  (void)now;
}

#ifdef ESPG_HV_ADC
void UdpBlipModule::emitHv(uint32_t now) {
  uint8_t buf[64];
  BlipHv h = { (float)hv.hvReading.get(), (float)hv.get_hv_target(),
               (uint32_t)hv.get_duty(), (int32_t)hv.get_duty_trim() };
  size_t n = blip_hv(buf, sizeof(buf), path(PSTR("hv")), h);
  if (n) sendPacket(buf, n);
  (void)now;
}
#endif

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
  strncpy_P(_path + BLIP_SUFFIX_AT, suffix, sizeof(_path) - BLIP_SUFFIX_AT - 1);
  return _path;
}

void UdpBlipModule::tryEmitClick(unsigned long now_ms) {
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  // never re-broadcast, feedback loop
  (void)now_ms;
  return;
#else
  if (_mode != 2) return;
  if (_fail_count >= UDPBLIP_FAIL_BACKOFF) return;
  if (!_udp) return;
  if (!Wifi::connected) return;
  if (ota_in_progress) return;
  uint32_t true_count = (uint32_t)gcounter.total_clicks + s_event_counter;
  if (true_count == _last_clicks) return;

  // whole intervals only, keeps the slack
  uint32_t elapsed = (uint32_t)(now_ms - _last_token_ms);
  if (elapsed >= UDPBLIP_CLICK_MIN_INTERVAL_MS) {
    uint32_t gained = (elapsed < UDPBLIP_CLICK_MIN_INTERVAL_MS * 2u)
                      ? 1u
                      : (elapsed / UDPBLIP_CLICK_MIN_INTERVAL_MS);
    uint32_t t = _burst_tokens + gained;
    if (t > UDPBLIP_CLICK_BURST_TOKENS) t = UDPBLIP_CLICK_BURST_TOKENS;
    _burst_tokens = (uint8_t)t;
    _last_token_ms += gained * UDPBLIP_CLICK_MIN_INTERVAL_MS;
  }
  if (_burst_tokens == 0) return;
  _burst_tokens--;

  _last_click_emit_ms = (uint32_t)now_ms;
  _last_clicks = true_count;
  emitClick(true_count, (uint32_t)now_ms);
#endif
}

void UdpBlipModule::notifyClick(unsigned long now_ms) {
  tryEmitClick(now_ms);
}

void UdpBlipModule::s_tick(unsigned long /*now_s*/) {
  if (_mode == 0 || ota_in_progress) return;
  if (!Wifi::connected) return;
  if (_udp && Wifi::connected_at_ms != _bound_at_ms) {
    Log::console(PSTR("UdpBlip: WiFi reconnect, rebinding"));
    _pending |= REBIND;
  } else if (!_udp || !_mdns_done) {
    _pending |= BIND;
  }

  if (_fail_count >= UDPBLIP_FAIL_BACKOFF) {
    if (++_backoff_ticks * 1000UL >= UDPBLIP_FAIL_COOLDOWN_MS) {
      _pending |= REBIND;
      _fail_count = 0;
      _backoff_ticks = 0;
    }
    return;
  }

  if (_emit_phase == 0) {
    if (++_stats_ticks * 1000UL >= UDPBLIP_STATS_INTERVAL_MS) {
      _stats_ticks = 0;
      _pending |= EMIT_RAD;
      _emit_phase = 1;
    }
  } else if (_emit_phase == 1) {
#ifdef ESPG_HV_ADC
    _pending |= EMIT_HV;
#endif
    _emit_phase = 2;
  } else {
    _sys_phase = !_sys_phase;
    if (_sys_phase) _pending |= EMIT_SYS | EMIT_ENV;
    _emit_phase = 0;
  }
}

uint16_t UdpBlipModule::loop_interval_ms() {
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  return 1000;
#else
  return UDPBLIP_CLICK_MIN_INTERVAL_MS;
#endif
}

void UdpBlipModule::loop(unsigned long now) {
  if (_pending) {
    uint8_t p = _pending;
    _pending = 0;
    if (p & (BIND | REBIND)) {
      if (p & REBIND) teardown();
      if (ensureUdp()) {
        _bound_at_ms = Wifi::connected_at_ms;
        announce_mdns();
      }
    }
    if (p & EMIT_RAD) emitRad(now);
#ifdef ESPG_HV_ADC
    if (p & EMIT_HV)  emitHv(now);
#endif
    if (p & EMIT_SYS) emitSys(now);
    if (p & EMIT_ENV) emitEnv(now);
  }
  tryEmitClick(now);
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
