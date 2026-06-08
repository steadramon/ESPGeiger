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
#include "../Counter/Counter.h"
#include "../GeigerInput/GeigerInput.h"  // s_event_counter (ISR pending)
#include "../Module/EGModuleRegistry.h"
#include "../Logger/Logger.h"
#include "../Prefs/EGPrefs.h"
#include "../Util/DeviceInfo.h"
#include "../Util/Wifi.h"
#include "../Util/FastMillis.h"
#include "../ArduinoOTA/ArduinoOTA.h"
#include "../GRNG/GRNG.h"
#include "../Util/TickProfile.h"
#include "../EnvSensor/EnvSensor.h"
#ifdef ESPG_HV_ADC
#include "../HV/HV.h"
extern HV hv;
#endif
#include <WiFiUdp.h>
#ifdef ESP8266
#include <ESP8266mDNS.h>
#else
#include <ESPmDNS.h>
#endif

extern Counter gcounter;

UdpBlipModule& udpblip = UdpBlipModule::getInstance();
EG_REGISTER_MODULE(udpblip)

EG_PSTR(UB_L_MODE,  "Mode");
EG_PSTR(UB_H_MODE,  "0=off, 1=telemetry only, 2=telemetry + per-click broadcast");
EG_PSTR(UB_L_GROUP, "Group / peer IP");
EG_PSTR(UB_H_GROUP, "239.x = multicast, else unicast peer.");
EG_PSTR(UB_P_GROUP, "[0-9.]+");
EG_PSTR(UB_L_PORT,  "Port");

static const EGPref UDPBLIP_PREF_ITEMS[] = {
  {"mode",  UB_L_MODE,  UB_H_MODE,  "0",                    nullptr,    0, 2, 0,  EGP_UINT,   0},
  {"group", UB_L_GROUP, UB_H_GROUP, UDPBLIP_DEFAULT_GROUP,  UB_P_GROUP, 0, 0, 24, EGP_STRING, 0},
  {"port",  UB_L_PORT,  nullptr,    UDPBLIP_DEFAULT_PORT,   nullptr,    1, 65535, 0, EGP_UINT, 0},
};

static const EGPrefGroup UDPBLIP_PREF_GROUP = {
  "udpblip", "Local Broadcast", 1,
  UDPBLIP_PREF_ITEMS,
  sizeof(UDPBLIP_PREF_ITEMS) / sizeof(UDPBLIP_PREF_ITEMS[0]),
};

const EGPrefGroup* UdpBlipModule::prefs_group() { return &UDPBLIP_PREF_GROUP; }

// OSC primitives: big-endian, 4-byte aligned. Encoders return 0 on overflow.

static inline size_t osc_pad(size_t n) { return (n + 3) & ~3u; }

static size_t osc_strn(uint8_t* buf, size_t cap, size_t off, const char* s, size_t len) {
  size_t padded = osc_pad(len + 1);
  if (off + padded > cap) return 0;
  memcpy(buf + off, s, len);
  for (size_t i = len; i < padded; i++) buf[off + i] = 0;
  return padded;
}

static inline size_t osc_str(uint8_t* buf, size_t cap, size_t off, const char* s) {
  return osc_strn(buf, cap, off, s, strlen(s));
}

static size_t osc_i32(uint8_t* buf, size_t cap, size_t off, uint32_t v) {
  if (off + 4 > cap) return 0;
  buf[off    ] = (uint8_t)(v >> 24);
  buf[off + 1] = (uint8_t)(v >> 16);
  buf[off + 2] = (uint8_t)(v >>  8);
  buf[off + 3] = (uint8_t)(v);
  return 4;
}

static size_t osc_f32(uint8_t* buf, size_t cap, size_t off, float v) {
  uint32_t u;
  memcpy(&u, &v, 4);
  return osc_i32(buf, cap, off, u);
}

// ---------- Module ----------

static constexpr size_t CLICK_PATH_LEN = 18;  // "/espg/CHIPID/click"

void UdpBlipModule::begin() {
  readPrefs();
  _last_clicks = gcounter.total_clicks;
  // GRNG-jitter the first telemetry burst so a synchronized fleet (power
  // cut, AP reboot) doesn't pile packets onto the receiver's lwIP rx mbox.
  // Counter starts already-aged by the jitter amount.
  uint16_t jitter_s = UDPBLIP_STATS_JITTER_MS
                    ? (uint16_t)((GRNG::fast_uint32() % UDPBLIP_STATS_JITTER_MS) / 1000UL) : 0;
  _stats_ticks = jitter_s;
  _backoff_ticks = 0;
  _fail_count = 0;
  // chipid is fixed at boot - pre-format the OSC paths once.
  snprintf_P(_click_path, sizeof(_click_path),
             PSTR("/espg/%s/click"), DeviceInfo::chipid());
  // /click OSC template: path+typetag baked here, emitClick patches the i32s.
  memcpy(_click_packet, _click_path, CLICK_PATH_LEN);
  _click_packet[20] = ','; _click_packet[21] = 'i'; _click_packet[22] = 'i';
  snprintf_P(_rad_path, sizeof(_rad_path),
             PSTR("/espg/%s/rad"), DeviceInfo::chipid());
  snprintf_P(_sys_path, sizeof(_sys_path),
             PSTR("/espg/%s/sys"), DeviceInfo::chipid());
  snprintf_P(_env_path, sizeof(_env_path),
             PSTR("/espg/%s/env"), DeviceInfo::chipid());
#ifdef ESPG_HV_ADC
  snprintf_P(_hv_path, sizeof(_hv_path),
             PSTR("/espg/%s/hv"), DeviceInfo::chipid());
#endif
  if (_mode > 0) {
    Log::console(PSTR("UdpBlip: mode=%u group=%s port=%u"),
                 _mode,
                 EGPrefs::getString("udpblip", "group"),
                 _port);
  }
}

void UdpBlipModule::readPrefs() {
  _mode = (uint8_t)EGPrefs::getUInt("udpblip", "mode");
  const char* gstr = EGPrefs::getString("udpblip", "group");
  if (!gstr || !gstr[0] || !_group.fromString(gstr)) {
    _group.fromString(UDPBLIP_DEFAULT_GROUP);
  }
  uint32_t p = EGPrefs::getUInt("udpblip", "port");
  _port = (p > 0 && p < 65536) ? (uint16_t)p : 5005;
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
  // GRNG-seeded source port in the RFC 6056 dynamic range so multiple
  // ESPGeigers on the same LAN don't all share lwIP's default 60288.
  // Stable across teardown (only re-seeded if _src_port is still 0).
  if (_src_port == 0) {
    _src_port = (uint16_t)(49152u + (GRNG::fast_uint32() & 0x3FFFu));
  }
  _udp->begin(_src_port);
  return true;
}

void UdpBlipModule::announce_mdns() {
  if (_mdns_done || _mode == 0) return;
  // ArduinoOTA::begin starts the MDNS responder. We can't probe its state
  // cross-platform, so just defer by uptime to be safely after OTA::begin.
  if (fast_millis() < 8000) return;
  MDNS.addService("osc", "udp", _port);
  const char* fname = DeviceInfo::friendlyName();
  if (fname && fname[0]) MDNS.addServiceTxt("osc", "udp", "fname", fname);
  MDNS.addServiceTxt("osc", "udp", "id", DeviceInfo::chipid());
  MDNS.addServiceTxt("osc", "udp", "group", EGPrefs::getString("udpblip", "group"));
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
  // Resync latches so enabling mode=2 doesn't fire every since-boot click in
  // one burst, and so stats waits a full interval while WiFi resettles.
  _last_clicks = (uint32_t)gcounter.total_clicks;
  _stats_ticks = 0;
  Log::console(PSTR("UdpBlip: prefs saved, mode=%u port=%u"), _mode, _port);
}

bool UdpBlipModule::sendPacket(const uint8_t* buf, size_t len) {
  if (!_udp) return false;
  if (!Wifi::connected) return false;
#ifdef ESP8266
  // ESP8266 needs beginPacketMulticast for IGMP; ESP32's beginPacket
  // handles either based on dest IP.
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
  // Patch counter + ts_ms into the pre-built template (aligned word stores).
  uint32_t cnt_be = __builtin_bswap32(counter);
  uint32_t ts_be  = __builtin_bswap32(ts_ms);
  memcpy(&_click_packet[24], &cnt_be, 4);
  memcpy(&_click_packet[28], &ts_be,  4);
  sendPacket(_click_packet, sizeof(_click_packet));
}

void UdpBlipModule::emitRad(uint32_t now) {
  uint8_t buf[96];
  size_t off = 0;
  const char* state =
    gcounter.is_alert()   ? "alert" :
    gcounter.is_warning() ? "warning" :
    gcounter.is_warm()    ? "healthy" : "warming";
  size_t n;
  if (!(n = osc_str(buf, sizeof(buf), off, _rad_path))) return; off += n;
  if (!(n = osc_strn(buf, sizeof(buf), off, ",ffsi", 5))) return; off += n;
  if (!(n = osc_f32(buf, sizeof(buf), off, gcounter.get_cpmf()))) return; off += n;
  if (!(n = osc_f32(buf, sizeof(buf), off, gcounter.get_usv()))) return; off += n;
  if (!(n = osc_str(buf, sizeof(buf), off, state))) return; off += n;
  if (!(n = osc_i32(buf, sizeof(buf), off, (uint32_t)gcounter.total_clicks))) return; off += n;
  sendPacket(buf, off);
  (void)now;
}

#ifdef ESPG_HV_ADC
void UdpBlipModule::emitHv(uint32_t now) {
  // /espg/{id}/hv ,ffii  reading_v target_v duty trim
  uint8_t buf[64];
  size_t off = 0;
  size_t n;
  if (!(n = osc_str(buf, sizeof(buf), off, _hv_path))) return; off += n;
  if (!(n = osc_strn(buf, sizeof(buf), off, ",ffii", 5))) return; off += n;
  if (!(n = osc_f32(buf, sizeof(buf), off, hv.hvReading.get()))) return; off += n;
  if (!(n = osc_f32(buf, sizeof(buf), off, (float)hv.get_hv_target()))) return; off += n;
  if (!(n = osc_i32(buf, sizeof(buf), off, (uint32_t)hv.get_duty()))) return; off += n;
  if (!(n = osc_i32(buf, sizeof(buf), off, (uint32_t)(int32_t)hv.get_duty_trim()))) return; off += n;
  sendPacket(buf, off);
  (void)now;
}
#endif

void UdpBlipModule::emitEnv(uint32_t now) {
  // /espg/{id}/env ,fff  temp_c humidity pressure_hpa (humidity = NaN on BMP280)
  // localPresent() only - never re-broadcast values we received over UDP.
  if (!envsensor.localPresent()) return;
  uint8_t buf[64];
  size_t off = 0;
  size_t n;
  if (!(n = osc_str(buf, sizeof(buf), off, _env_path))) return; off += n;
  if (!(n = osc_strn(buf, sizeof(buf), off, ",fff", 4))) return; off += n;
  if (!(n = osc_f32(buf, sizeof(buf), off, envsensor.tempC()))) return; off += n;
  if (!(n = osc_f32(buf, sizeof(buf), off, envsensor.humidity()))) return; off += n;
  if (!(n = osc_f32(buf, sizeof(buf), off, envsensor.pressure()))) return; off += n;
  sendPacket(buf, off);
  (void)now;
}

void UdpBlipModule::emitSys(uint32_t now) {
  uint8_t buf[64];
  size_t off = 0;
  size_t n;
  if (!(n = osc_str(buf, sizeof(buf), off, _sys_path))) return; off += n;
  if (!(n = osc_strn(buf, sizeof(buf), off, ",iiii", 5))) return; off += n;
  if (!(n = osc_i32(buf, sizeof(buf), off, (uint32_t)(fast_millis() / 1000UL)))) return; off += n;
  if (!(n = osc_i32(buf, sizeof(buf), off, (uint32_t)(int32_t)Wifi::rssi))) return; off += n;
  if (!(n = osc_i32(buf, sizeof(buf), off, TickProfile::lps))) return; off += n;
  if (!(n = osc_i32(buf, sizeof(buf), off, TickProfile::tick_max_us))) return; off += n;
  sendPacket(buf, off);
  (void)now;
}

// Single emit path used by both notifyClick (high-freq) and loop() (tail
// catcher). Throttle caps emit rate; deferred clicks accumulate in the
// cumulative counter so the receiver gap-fills via delta regardless.
void UdpBlipModule::tryEmitClick(unsigned long now_ms) {
#if GEIGER_IS_UDPRX(GEIGER_TYPE)
  // Receivers must never re-broadcast received clicks (feedback loop).
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

  // Token bucket; _last_token_ms advances only by converted intervals so
  // sub-interval slack carries forward. Common path fast-skips the divide.
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

// 1 Hz scheduler for telemetry + cooldown. Counter-based, no millis() math.
void UdpBlipModule::s_tick(unsigned long /*now_s*/) {
  if (_mode == 0 || ota_in_progress) return;
  if (!Wifi::connected) return;
  // WiFi reconnect can leave egress netif + mDNS stale; rebind both.
  if (_udp && Wifi::connected_at_ms != _bound_at_ms) {
    Log::console(PSTR("UdpBlip: WiFi reconnect, rebinding"));
    teardown();
    _mdns_done = false;
  }
  if (!ensureUdp()) return;
  _bound_at_ms = Wifi::connected_at_ms;
  if (!_mdns_done) announce_mdns();

  if (_fail_count >= UDPBLIP_FAIL_BACKOFF) {
    if (++_backoff_ticks * 1000UL >= UDPBLIP_FAIL_COOLDOWN_MS) {
      _fail_count = 0;
      _backoff_ticks = 0;
    }
    return;
  }

  // Schedule emits; loop() services the actual UDP sends.
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
  return 60000;   // UDPRX: tryEmitClick is a no-op; loop body is empty
#else
  return UDPBLIP_CLICK_MIN_INTERVAL_MS;
#endif
}

// Producer-only tail-catcher: flush any click counter the throttle deferred
// when a burst ends without further notifyClick calls. No-op on UDPRX.
// Also services pending telemetry emits set by s_tick - keeps the ~700us
// UDP sends out of tick_max.
void UdpBlipModule::loop(unsigned long now) {
  if (_pending) {
    uint8_t p = _pending;
    _pending = 0;
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
  return snprintf_P(buf, cap,
    PSTR("\"udpblip\":{\"mode\":%u,\"group\":\"%s\",\"port\":%u,\"fails\":%u}"),
    _mode,
    EGPrefs::getString("udpblip", "group"),
    _port,
    _fail_count);
}
