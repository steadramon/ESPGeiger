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
#include "../GeigerInput/GeigerInput.h"  // eventCounter1/2 (ISR pending)
#include "../Module/EGModuleRegistry.h"
#include "../Logger/Logger.h"
#include "../Prefs/EGPrefs.h"
#include "../Util/DeviceInfo.h"
#include "../Util/Wifi.h"
#include "../ArduinoOTA/ArduinoOTA.h"
#include "../GRNG/GRNG.h"
#include "../Util/TickProfile.h"
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
EG_PSTR(UB_H_MODE,  "0=off, 1=stats only (60s), 2=stats + per-click blip");
EG_PSTR(UB_L_GROUP, "Multicast group");
EG_PSTR(UB_H_GROUP, "239.x.x.x site-local. All devices on a LAN share one group.");
EG_PSTR(UB_P_GROUP, "[0-9.]+");
EG_PSTR(UB_L_PORT,  "Port");

static const EGPref UDPBLIP_PREF_ITEMS[] = {
  {"mode",  UB_L_MODE,  UB_H_MODE,  "0",                    nullptr,    0, 2, 0,  EGP_UINT,   0},
  {"group", UB_L_GROUP, UB_H_GROUP, UDPBLIP_DEFAULT_GROUP,  UB_P_GROUP, 0, 0, 24, EGP_STRING, 0},
  {"port",  UB_L_PORT,  nullptr,    UDPBLIP_DEFAULT_PORT,   nullptr,    1, 65535, 0, EGP_UINT, 0},
};

static const EGPrefGroup UDPBLIP_PREF_GROUP = {
  "udpblip", "Local broadcast", 1,
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

void UdpBlipModule::begin() {
  readPrefs();
  _last_clicks = gcounter.total_clicks;
  // GRNG phase-shift the first /stats emission so a fleet booting together
  // (power-cut, AP reboot) doesn't synchronize bursts onto the receiver's
  // lwIP rx mbox. Persistent across all subsequent cycles.
  uint32_t jitter = UDPBLIP_STATS_JITTER_MS
                  ? (GRNG::fast_uint32() % UDPBLIP_STATS_JITTER_MS) : 0;
  _last_stats_ms = millis() - jitter;
  _fail_count = 0;
  // chipid is fixed at boot - pre-format the OSC paths once.
  snprintf_P(_click_path, sizeof(_click_path),
             PSTR("/espg/%s/click"), DeviceInfo::chipid());
  snprintf_P(_rad_path, sizeof(_rad_path),
             PSTR("/espg/%s/rad"), DeviceInfo::chipid());
  snprintf_P(_sys_path, sizeof(_sys_path),
             PSTR("/espg/%s/sys"), DeviceInfo::chipid());
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
  // Clicks push from Counter; loop() only handles setup + periodic telemetry.
  EGModuleRegistry::set_loop_interval(this, _mode == 0 ? 60000 : 1000);
}

void UdpBlipModule::readPrefs() {
  _mode = (uint8_t)EGPrefs::getUInt("udpblip", "mode");
  const char* gstr = EGPrefs::getString("udpblip", "group");
  if (!gstr || !gstr[0] || !_group.fromString(gstr)) {
    _group.fromString(UDPBLIP_DEFAULT_GROUP);
  }
  uint32_t p = EGPrefs::getUInt("udpblip", "port");
  _port = (p > 0 && p < 65536) ? (uint16_t)p : 5005;
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
  if (millis() < 8000) return;
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
  _last_stats_ms = millis();
  EGModuleRegistry::set_loop_interval(this, _mode == 0 ? 60000 : 1000);
  Log::console(PSTR("UdpBlip: prefs saved, mode=%u port=%u"), _mode, _port);
}

bool UdpBlipModule::sendPacket(const uint8_t* buf, size_t len) {
  if (!_udp) return false;
  if (!Wifi::connected) return false;
  // ESP8266's WiFiUDP needs a multicast-specific begin that takes the
  // source IP; ESP32 routes via standard beginPacket on 224.0.0.0/4 dest.
#ifdef ESP8266
  if (!_udp->beginPacketMulticast(_group, _port, Wifi::local_ip)) {
    _fail_count++;
    return false;
  }
#else
  if (!_udp->beginPacket(_group, _port)) {
    _fail_count++;
    return false;
  }
#endif
  _udp->write(buf, len);
  if (_udp->endPacket() == 0) {
    _fail_count++;
    return false;
  }
  _fail_count = 0;
  return true;
}

static constexpr size_t CLICK_PATH_LEN = 18;

void UdpBlipModule::emitClick(uint32_t counter, uint32_t ts_ms) {
  uint8_t buf[96];
  size_t off = 0;
  size_t n;
  if (!(n = osc_strn(buf, sizeof(buf), off, _click_path, CLICK_PATH_LEN))) return; off += n;
  if (!(n = osc_strn(buf, sizeof(buf), off, ",ii", 3))) return; off += n;
  if (!(n = osc_i32(buf, sizeof(buf), off, counter))) return; off += n;
  if (!(n = osc_i32(buf, sizeof(buf), off, ts_ms))) return; off += n;
  sendPacket(buf, off);
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

void UdpBlipModule::emitSys(uint32_t now) {
  uint8_t buf[96];
  size_t off = 0;
  size_t n;
  if (!(n = osc_str(buf, sizeof(buf), off, _sys_path))) return; off += n;
  if (!(n = osc_strn(buf, sizeof(buf), off, ",iiiiii", 7))) return; off += n;
  if (!(n = osc_i32(buf, sizeof(buf), off, (uint32_t)(millis() / 1000UL)))) return; off += n;
  if (!(n = osc_i32(buf, sizeof(buf), off, (uint32_t)(int32_t)Wifi::rssi))) return; off += n;
  if (!(n = osc_i32(buf, sizeof(buf), off, (uint32_t)ESP.getFreeHeap()))) return; off += n;
  if (!(n = osc_i32(buf, sizeof(buf), off, (uint32_t)DeviceInfo::heapFrag()))) return; off += n;
  if (!(n = osc_i32(buf, sizeof(buf), off, TickProfile::lps))) return; off += n;
  if (!(n = osc_i32(buf, sizeof(buf), off, TickProfile::tick_max_us))) return; off += n;
  sendPacket(buf, off);
  (void)now;
}

void UdpBlipModule::notifyClick(unsigned long now_ms) {
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
  // total_clicks lags 1 Hz; eventCounter1+2 is the live ISR-pending
  // count. Sum is ISR-accurate, so a counter jump credits any clicks
  // the main loop missed catching individually.
  uint32_t true_count = (uint32_t)gcounter.total_clicks +
                        (uint32_t)(eventCounter1 + eventCounter2);
  if (true_count == _last_clicks) return;
  _last_clicks = true_count;
  emitClick((uint32_t)_last_clicks, (uint32_t)now_ms);
#endif
}

void UdpBlipModule::loop(unsigned long now) {
  if (_mode == 0 || ota_in_progress) return;
  if (!Wifi::connected) return;
  if (!ensureUdp()) return;
  if (!_mdns_done) announce_mdns();

  // After FAIL_BACKOFF consecutive send failures, cool off for one stats
  // period so we don't hammer a sick WiFi stack.
  if (_fail_count >= UDPBLIP_FAIL_BACKOFF) {
    if ((unsigned long)(now - _last_stats_ms) >= UDPBLIP_STATS_INTERVAL_MS) {
      _fail_count = 0;
      _last_stats_ms = now;
    }
    return;
  }

  if (_emit_phase == 0) {
    if ((unsigned long)(now - _last_stats_ms) >= UDPBLIP_STATS_INTERVAL_MS) {
      _last_stats_ms = now;
      emitRad(now);
      _emit_phase = 1;
    }
  } else if (_emit_phase == 1) {
#ifdef ESPG_HV_ADC
    emitHv(now);
#endif
    _emit_phase = 2;
  } else {
    _sys_phase = !_sys_phase;
    if (_sys_phase) emitSys(now);
    _emit_phase = 0;
  }
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
