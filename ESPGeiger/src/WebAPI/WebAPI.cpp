/*
  WebAPI.cpp - Signed-submission remote API client.

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
#ifdef WEBAPIOUT

#ifndef WEBAPI_URL
#define WEBAPI_URL "http://api.espgeiger.com"
#endif

#include <Arduino.h>
#include <EGHttpServer.h>
#include "WebAPI.h"
#include <EGBase64.h>   // buffer-in/out, no heap (vs core's heap-using <base64.h>)
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Prefs/EGPrefs.h"
#include "../Util/DeviceInfo.h"
#include "../Util/Wifi.h"
#include "../NTP/NTP.h"
#include "../GRNG/sha256.h"
#include "../GRNG/GRNG.h"
#include "../Util/TickProfile.h"
#include "../WebPortal/WebPortal.h"
#include "MsgPack.h"
#include <FS.h>
#include <LittleFS.h>
#include <uECC.h>
#ifdef ESPG_HV_ADC
#include "../HV/HV.h"
#endif

WebAPI webapi;
EG_REGISTER_MODULE(webapi)

// 0=Off (silent), 1=Heartbeat (presence + health every 15 min), 2=Readings (full).
// 0,0 lat/lon is the "unset" signal so server falls back to IP-based geo.
EG_PSTR(WA_L_MD,  "Sharing mode");
EG_PSTR(WA_H_MD,  "0=Off, 1=Heartbeat, 2=CPM Readings");
EG_PSTR(WA_L_LAT, "Latitude");
EG_PSTR(WA_H_LAT, "Station latitude  (-90..90, 0 = use IP)");
EG_PSTR(WA_L_LON, "Longitude");
EG_PSTR(WA_H_LON, "Station longitude (-180..180, 0 = use IP)");

static const EGPref WEBAPI_PREF_ITEMS[] = {
  {"mode",   WA_L_MD,  WA_H_MD,  "2", nullptr, 0,    2,   0, EGP_UINT,  0},
  {"lat",    WA_L_LAT, WA_H_LAT, "0", nullptr, -90,  90,  0, EGP_FLOAT, 0},
  {"lon",    WA_L_LON, WA_H_LON, "0", nullptr, -180, 180, 0, EGP_FLOAT, 0},
};

static const EGPrefGroup WEBAPI_PREF_GROUP = {
  "webapi", "ESPGeiger Network", 3,
  WEBAPI_PREF_ITEMS,
  sizeof(WEBAPI_PREF_ITEMS) / sizeof(WEBAPI_PREF_ITEMS[0]),
};

const EGPrefGroup* WebAPI::prefs_group() { return &WEBAPI_PREF_GROUP; }

void WebAPI::on_prefs_loaded() {
  _mode = (uint8_t)EGPrefs::getUInt("webapi", "mode");
  EGModuleRegistry::set_loop_interval(this, _mode == 0 ? 60000 : 1000);
}

static int webapi_rng(uint8_t *dest, unsigned size) {
  GRNG::extract(dest, size);
  return 1;
}

static void strtobytes(const char* str, uint8_t* bytes, int count) {
  for (int c = 0; c < count; ++c) {
    if (sscanf(str, "%2hhx", &bytes[c]) != 1) return;
    str += 2;
  }
}

static void bytestostr(const uint8_t* array, unsigned int len, char* buffer) {
  for (unsigned int i = 0; i < len; i++) {
    buffer[i*2+0] = "0123456789abcdef"[array[i]>>4];
    buffer[i*2+1] = "0123456789abcdef"[array[i]&0xf];
  }
  buffer[len*2] = '\0';
}

WebAPI::WebAPI() {}

void WebAPI::begin() {
  uECC_set_rng(&webapi_rng);
  GRNG::stir();
  loadConfig();
  Log::debug(PSTR("WebAPI: Ready - endpoint=%s, key=%s"),
    WEBAPI_URL, priv_k[0] == 0 ? "none (will generate)" : "loaded");
}

void WebAPI::loop(unsigned long now) {
  if (_mode == 0) return;
  if (!ntpclient.synced) {
    EGModuleRegistry::set_loop_interval(this, 1000);
    return;
  }

  if (lastHandshake == 0) {
    lastHandshake = now + random(11311) - WEBAPI_HANDSHAKE_MS;
  } else if ((now - lastHandshake) >= WEBAPI_HANDSHAKE_MS) {
    doHandshake();
  } else if (station_id != 0) {
    if (lastPing == 0) {
      lastPing = staggeredPingStart(now);
    } else if ((now - lastPing) >= pingIntervalMs) {
      lastPing += pingIntervalMs;
      if ((now - lastPing) >= pingIntervalMs) lastPing = staggeredPingStart(now);

      const bool healthDue = (healthPostCounter == 0);
      if (_mode == 2 || healthDue) {
        // Heartbeat-only mode never sends radiation; the cadence reduces
        // to one post per WEBAPI_HEALTH_EVERY ticks.
        postMeasurement(_mode != 2);
      } else {
        // Heartbeat skip: keep the per-15-min cadence aligned.
        if (++healthPostCounter >= WEBAPI_HEALTH_EVERY) healthPostCounter = 0;
      }
    }
  }

  // Sleep until whichever target fires next: handshake or ping. station_id
  // gate means before the first handshake completes we only watch the
  // handshake target.
  unsigned long target = lastHandshake + WEBAPI_HANDSHAKE_MS;
  if (station_id != 0 && lastPing != 0) {
    unsigned long ping_target = lastPing + pingIntervalMs;
    if ((long)(ping_target - target) < 0) target = ping_target;
  }
  EGModuleRegistry::sleep_until(this, now, target);
}

static uint32_t wallClockWaitMs(uint32_t k, uint32_t intervalMs, uint32_t period_s) {
  uint32_t target_ms = k % intervalMs;
  uint32_t cur_ms    = (uint32_t)((time(NULL) % period_s) * 1000UL);
  return (target_ms >= cur_ms) ? target_ms - cur_ms
                               : intervalMs - cur_ms + target_ms;
}

unsigned long WebAPI::staggeredPingStart(unsigned long now) const {
  uint32_t k = ((uint32_t)pub_k[0] << 24) | ((uint32_t)pub_k[1] << 16)
             | ((uint32_t)pub_k[2] << 8)  |  (uint32_t)pub_k[3];
  return now + wallClockWaitMs(k, pingIntervalMs, 60) - pingIntervalMs;
}

void WebAPI::doHandshake() {
  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("WebAPI: Testmode"));
    lastHandshake = millis();
    return;
  }

  GRNG::stir();

  if (priv_k[0] == 0) {
    Log::console(PSTR("WebAPI: Generating keys"));
    GRNG::stir();
    uECC_make_key(pub_k, priv_k, uECC_secp192r1());
    optimistic_yield(100 * 1000);
    encode_base64(pub_k, 48, (uint8_t *)pub_k_64);
    saveConfig();
  }

  Log::debug(PSTR("WebAPI: Handshake -> %s/api/1/handshake"), WEBAPI_URL);

  // 256B leaves headroom for long version strings (MsgPack encodes ~200B).
  uint8_t buffer[256];

  // Append the git short-sha for non-tagged builds so the server can
  // distinguish multiple devel builds.
  char versionStr[32];
  if (GIT_VERSION[0] != '\0') {
    snprintf_P(versionStr, sizeof(versionStr), PSTR("%s/%s"), RELEASE_VERSION, GIT_VERSION);
  } else {
    strncpy(versionStr, RELEASE_VERSION, sizeof(versionStr) - 1);
    versionStr[sizeof(versionStr) - 1] = '\0';
  }

  // 2dp truncation (~1km) for privacy; full precision stays on device.
  // 0,0 means "use IP" so we omit the fields entirely.
  float latF = EGPrefs::getFloat("webapi", "lat");
  float lonF = EGPrefs::getFloat("webapi", "lon");
  bool sendCoords = (latF != 0.0f || lonF != 0.0f);

  // Crash details on the FIRST post-boot handshake only, so we don't
  // re-send the same exception across the rest of an uptime.
  uint32_t epc1 = 0, excvaddr = 0;
  uint8_t exccause = 0;
  bool sendExc = !_exc_sent && DeviceInfo::resetExc(&epc1, &excvaddr, &exccause);

  MsgPack::Writer mp(buffer, sizeof(buffer));
  uint8_t entries = 11;
  if (sendCoords) entries += 2;
  if (sendExc)    entries += 3;   // ec, pc, va
  mp.map(entries);
  mp.kv("n",  (uint32_t)time(NULL));
  mp.kv("pk", pub_k_64);
  mp.kv("ci", DeviceInfo::chipid());
  mp.kv("v",  versionStr);
  mp.kv("bd", DeviceInfo::chipmodel());
  mp.kv("gm", BUILD_ENV);
  mp.kv("gc", DeviceInfo::geigermodel());
  mp.kv("td", (uint32_t)DeviceInfo::tubeDetection());
  mp.kv("fl", (uint32_t)DeviceInfo::featureFlags());
  mp.kv("rr", DeviceInfo::resetReason());
  mp.kv("m",  (uint32_t)_mode);
  if (sendCoords) {
    mp.kv("la", (int32_t)(latF * 100.0f + (latF >= 0 ? 0.5f : -0.5f)) * 0.01f);
    mp.kv("lo", (int32_t)(lonF * 100.0f + (lonF >= 0 ? 0.5f : -0.5f)) * 0.01f);
  }
  if (sendExc) {
    mp.kv("ec", (uint32_t)exccause);
    mp.kv("pc", epc1);
    mp.kv("va", excvaddr);
  }
  if (mp.overflow) {
    Log::console(PSTR("WebAPI: Handshake encode overflow"));
    return;
  }
  size_t bodyLen = mp.length();

  uint8_t signature[48];
  uint8_t shahash[HASH_LENGTH];
  Sha256.flush();
  Sha256.init();
  Sha256.write(buffer, bodyLen);
  memcpy(shahash, Sha256.result(), HASH_LENGTH);
  optimistic_yield(100 * 1000);
  if (uECC_sign(priv_k, shahash, HASH_LENGTH, signature, uECC_secp192r1()) != 1) {
    lastHandshake = millis() - WEBAPI_HANDSHAKE_MS + _hs_backoff_ms;
    _hs_backoff_ms = min(_hs_backoff_ms * 2, (uint32_t)(5UL * 60UL * 1000UL));
    return;
  }
  optimistic_yield(100 * 1000);

  char basesig[72];
  encode_base64(signature, sizeof(signature), (uint8_t *)basesig);

  if (request.readyState() == readyStateUnsent || request.readyState() == readyStateDone) {
    if (request.open("POST", WEBAPI_URL "/api/1/handshake")) {
      led.Blink(500, 500);
      request.setReqHeader(F("User-Agent"), DeviceInfo::useragent());
      request.setReqHeader(F("Content-Type"), F("application/msgpack"));
      request.setReqHeader(F("X-Auth"), basesig);
      request.onReadyStateChange(httpHandshakeCb, this);
      request.setTimeout(10);
      request.send(buffer, bodyLen);
      lastHandshake = millis();
      send_indicator = 2;
    }
  }
}

void WebAPI::httpHandshakeCb(void *optParm, AsyncHTTPRequest *request, int readyState) {
  if (readyState != readyStateDone) return;
  WebAPI* self = static_cast<WebAPI*>(optParm);
  self->last_attempt_ms = millis();
  self->last_ok = false;
  if (request->responseHTTPcode() == 200) {
    // String() round-trips raw bytes; treat as binary.
    String r = request->responseText();
    MsgPack::Reader reader((const uint8_t*)r.c_str(), r.length());
    uint32_t id = 0;
    if (reader.find_key("id")) {
      reader.read_uint(&id);
    }
    if (!reader.error) {
      if (id == 0) {
        Log::debug(PSTR("WebAPI: Handshake rejected (no ID in response)"));
        self->lastHandshake = millis() - WEBAPI_HANDSHAKE_MS + self->_hs_backoff_ms;
        self->_hs_backoff_ms = min(self->_hs_backoff_ms * 2, (uint32_t)(5UL * 60UL * 1000UL));
        EGModuleRegistry::set_loop_interval(self, 100);
        self->note_publish(false);
        return;
      }
      if (self->station_id != id) {
        Log::console(PSTR("WebAPI: Handshake OK - station ID %u"), id);
        self->station_id = id;
        uint32_t k = ((uint32_t)self->pub_k[4] << 24) | ((uint32_t)self->pub_k[5] << 16)
                   | ((uint32_t)self->pub_k[6] << 8)  |  (uint32_t)self->pub_k[7];
        self->lastHandshake = millis() + wallClockWaitMs(k, WEBAPI_HANDSHAKE_MS, 3600) - WEBAPI_HANDSHAKE_MS;
        EGModuleRegistry::set_loop_interval(self, 100);
      } else {
        Log::debug(PSTR("WebAPI: Handshake OK - station ID %u"), id);
      }
      self->last_ok = true;
      self->_hs_backoff_ms = 30000UL;
      self->_exc_sent = true;     // crash info delivered (or none was sent)
      self->note_publish(true);
      return;
    }
    Log::debug(PSTR("WebAPI: Handshake parse error"));
  } else {
    int code = request->responseHTTPcode();
    Log::debug(PSTR("WebAPI: Handshake error %d - %s"),
      code, request->responseHTTPString().c_str());
    if (code == 429) {
      Log::debug(PSTR("WebAPI: Rate-limited; waiting for next scheduled interval"));
      self->note_publish(false);
      return;
    }
  }
  // Backoff prevents a uECC_sign storm while the server is down.
  self->lastHandshake = millis() - WEBAPI_HANDSHAKE_MS + self->_hs_backoff_ms;
  self->_hs_backoff_ms = min(self->_hs_backoff_ms * 2, (uint32_t)(5UL * 60UL * 1000UL));
  EGModuleRegistry::set_loop_interval(self, 100);
  self->note_publish(false);
}

void WebAPI::postMeasurement(bool censusOnly) {
  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("WebAPI: Testmode"));
    return;
  }

  // Wait for a full CPM bucket so the first post isn't noisy.
  if (!gcounter.is_warm()) {
    Log::debug(PSTR("WebAPI: Skipping early post (CPM bucket not full)"));
    return;
  }

  Log::debug(PSTR("WebAPI: Posting station %u%s ..."), station_id, censusOnly ? " (census)" : "");

  // 192B handles full health-tick post (~70B) plus headroom.
  uint8_t buffer[192];

  // Pre-count entries for the MsgPack fixmap header.
  uint8_t entries = 2;  // id, n
  bool sendRadiation = !censusOnly;
  bool sendHealth = (healthPostCounter == 0);
#ifdef ESPG_HV_ADC
  bool sendHv = sendRadiation && hv.get_pwm_pin() >= 0 && hv.get_vd_ratio() != 0;
#endif
  if (sendRadiation) {
    entries += 2;  // cpm, usv
  }
#ifdef ESPG_HV_ADC
  if (sendHv) entries += 1;  // hv
#endif
  if (sendHealth) entries += 5;  // ut, mem, lps, tk, rssi

  MsgPack::Writer mp(buffer, sizeof(buffer));
  mp.map(entries);
  mp.kv("id", station_id);
  mp.kv("n",  (uint32_t)time(NULL));

  // float32 keeps the payload compact and avoids the soft-float printer.
  if (sendRadiation) {
    mp.kv("c",  gcounter.get_cpmf());
    mp.kv("u",  gcounter.get_usv());
  }
#ifdef ESPG_HV_ADC
  if (sendHv) mp.kv("hv", hv.hvReading.get());
#endif

  // Health snapshot rides the regular post every WEBAPI_HEALTH_EVERY ticks.
  if (sendHealth) {
    mp.kv("ut", (uint32_t)DeviceInfo::uptime());
    mp.kv("mh", DeviceInfo::freeHeap());
    mp.kv("l",  (uint32_t)TickProfile::lps);
    mp.kv("tk", (uint32_t)TickProfile::tick_max_us);
    mp.kv("rs", (int32_t)Wifi::rssi);
  }

  if (mp.overflow) {
    Log::console(PSTR("WebAPI: Post encode overflow"));
    return;
  }
  size_t bodyLen = mp.length();

  uint8_t signature[48];
  uint8_t shahash[HASH_LENGTH];
  Sha256.flush();
  Sha256.init();
  Sha256.write(buffer, bodyLen);
  memcpy(shahash, Sha256.result(), HASH_LENGTH);
  optimistic_yield(100 * 1000);
  if (uECC_sign(priv_k, shahash, HASH_LENGTH, signature, uECC_secp192r1()) != 1) {
    lastPing -= pingIntervalMs;  // retry next loop
    return;
  }
  optimistic_yield(100 * 1000);

  char basesig[72];
  encode_base64(signature, sizeof(signature), (uint8_t *)basesig);

  if (request.readyState() == readyStateUnsent || request.readyState() == readyStateDone) {
    if (request.open("POST", WEBAPI_URL "/api/1/post")) {
      led.Blink(500, 500);
      request.setReqHeader(F("User-Agent"), DeviceInfo::useragent());
      request.setReqHeader(F("Content-Type"), F("application/msgpack"));
      request.setReqHeader(F("X-Auth"), basesig);
      request.onReadyStateChange(httpRequestCb, this);
      request.setTimeout(10);
      request.send(buffer, bodyLen);
      send_indicator = 2;
    }
  }
}

void WebAPI::httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState) {
  if (readyState != readyStateDone) return;
  WebAPI* self = static_cast<WebAPI*>(optParm);
  self->last_attempt_ms = millis();
  self->last_ok = false;
  if (request->responseHTTPcode() == 200) {
    // Server returns MsgPack { "ok": true } on success. find_key skips
    // any future advisory fields without breaking older firmware.
    String r = request->responseText();
    MsgPack::Reader reader((const uint8_t*)r.c_str(), r.length());
    bool ok = false;
    if (reader.find_key("ok")) reader.read_bool(&ok);
    if (!reader.error && ok) {
      Log::debug(PSTR("WebAPI: Post OK - station %u"), self->station_id);
      self->last_ok = true;
      if (++self->healthPostCounter >= WEBAPI_HEALTH_EVERY) self->healthPostCounter = 0;
    } else {
      Log::debug(PSTR("WebAPI: Post response not ok"));
    }
  } else {
    int code = request->responseHTTPcode();
    Log::debug(PSTR("WebAPI: Error %d - %s"), code, request->responseHTTPString().c_str());
    if (code == 403) {
      self->station_id = 0;
      self->lastHandshake = 0;
      EGModuleRegistry::set_loop_interval(self, 100);
    } else if (code == 409) {
      Log::debug(PSTR("WebAPI: Replay rejected (check NTP clock)"));
    }
  }
  self->note_publish(self->last_ok);
}

void WebAPI::forget() {
  if (station_id == 0 || priv_k[0] == 0) {
    Log::debug(PSTR("WebAPI: Forget skipped (no station)"));
    return;
  }
  if (!ntpclient.synced) {
    Log::console(PSTR("WebAPI: Forget skipped (clock not synced)"));
    return;
  }

  uint8_t buffer[24];
  MsgPack::Writer mp(buffer, sizeof(buffer));
  mp.map(2);
  mp.kv("id", station_id);
  mp.kv("n",  (uint32_t)time(NULL));
  if (mp.overflow) return;
  size_t bodyLen = mp.length();

  uint8_t signature[48];
  uint8_t shahash[HASH_LENGTH];
  Sha256.flush();
  Sha256.init();
  Sha256.write(buffer, bodyLen);
  memcpy(shahash, Sha256.result(), HASH_LENGTH);
  optimistic_yield(100 * 1000);
  if (uECC_sign(priv_k, shahash, HASH_LENGTH, signature, uECC_secp192r1()) != 1) {
    Log::console(PSTR("WebAPI: Forget sign failed"));
    return;
  }
  optimistic_yield(100 * 1000);

  char basesig[72];
  encode_base64(signature, sizeof(signature), (uint8_t *)basesig);

  if (request.readyState() == readyStateUnsent || request.readyState() == readyStateDone) {
    if (request.open("POST", WEBAPI_URL "/api/1/forget")) {
      Log::console(PSTR("WebAPI: Forget station %u"), station_id);
      led.Blink(500, 500);
      request.setReqHeader(F("User-Agent"), DeviceInfo::useragent());
      request.setReqHeader(F("Content-Type"), F("application/msgpack"));
      request.setReqHeader(F("X-Auth"), basesig);
      request.onReadyStateChange(httpForgetCb, this);
      request.setTimeout(10);
      request.send(buffer, bodyLen);
      send_indicator = 2;
    }
  }
}

void WebAPI::httpForgetCb(void *optParm, AsyncHTTPRequest *request, int readyState) {
  if (readyState != readyStateDone) return;
  WebAPI* self = static_cast<WebAPI*>(optParm);
  int code = request->responseHTTPcode();
  // 403 = unknown station, treat as already-gone.
  if (code == 200 || code == 403) {
    Log::console(PSTR("WebAPI: Station forgotten (HTTP %d)"), code);
    self->station_id = 0;
    self->lastHandshake = 0;
    self->lastPing = 0;
    self->healthPostCounter = 0;
    self->last_ok = true;
    EGModuleRegistry::set_loop_interval(self, 100);
  } else {
    Log::console(PSTR("WebAPI: Forget failed - %s"), request->responseHTTPString().c_str());
  }
  self->last_attempt_ms = millis();
  self->note_publish(self->last_ok);
}

void WebAPI::saveConfig() {
  Log::console(PSTR("WebAPI: Saving key ..."));
  // Don't end() the FS here; other modules share it.
  LittleFS.begin();
  File f = LittleFS.open("/api.key", "w");
  if (!f) {
    Log::console(PSTR("WebAPI: Failed to open api.key"));
    return;
  }
  char private_key[50] = "";
  bytestostr(priv_k, 24, private_key);
  f.println(private_key);
  f.close();
}

void WebAPI::loadConfig() {
  LittleFS.begin();
  if (LittleFS.exists("/api.key")) {
    File f = LittleFS.open("/api.key", "r");
    if (f) {
      String data = f.readStringUntil('\n');
      f.close();
      strtobytes(data.c_str(), priv_k, 24);
      if (uECC_compute_public_key(priv_k, pub_k, uECC_secp192r1()) == 1) {
        encode_base64(pub_k, 48, (uint8_t *)pub_k_64);
        Log::console(PSTR("WebAPI: Key loaded"));
      } else {
        priv_k[0] = 0;
        LittleFS.remove("/api.key");
        Log::console(PSTR("WebAPI: Stored key invalid, regenerating"));
      }
    }
  }
}

size_t WebAPI::status_json(char* buf, size_t cap, unsigned long now) {
  if (_mode == 0) return 0;
  return write_status_json(buf, cap, "webapi", last_ok, last_attempt_ms, now);
}

// ---------- HTTP routes ----------

static const char FORGET_BODY[] PROGMEM =
  "<p>A delete request has been sent to the server. Sharing is now <b>Off</b>.</p>"
  "<p class=muted>Re-enabling sharing later will register a new station; old readings will not be linked.</p>";

static void hWebAPI(EGHttpRequest& req, EGHttpResponse& res, void*) {
  bool saved = false;
  if (req.method() == EGHttpRequest::POST) {
    // arg() returns into a shared static buffer - atof/atoi consumes the
    // value before the next arg() call, and EGPrefs::put copies internally.
    const char* m = req.arg("m");
    if (m) {
      int mode = atoi(m);
      if (mode < 0 || mode > 2) mode = 0;
      char mbuf[2] = {(char)('0' + mode), '\0'};
      EGPrefs::put("webapi", "mode", mbuf);
    }
    // atof would pull _strtod_l (~3.7 KB flash).
    auto validDeg = [](const char* s, int maxWhole) -> bool {
      if (!s || !*s) return false;
      if (*s == '-' || *s == '+') s++;
      int w = 0; bool any = false;
      while (*s >= '0' && *s <= '9') {
        w = w * 10 + (*s++ - '0'); any = true;
        if (w > maxWhole) return false;
      }
      if (!any) return false;
      if (*s == '.') { s++; while (*s >= '0' && *s <= '9') s++; }
      return !*s;
    };
    const char* lat = req.arg("lat");
    if (lat) {
      if (!*lat) EGPrefs::put("webapi", "lat", "0");
      else if (validDeg(lat, 90)) EGPrefs::put("webapi", "lat", lat);
    }
    const char* lon = req.arg("lon");
    if (lon) {
      if (!*lon) EGPrefs::put("webapi", "lon", "0");
      else if (validDeg(lon, 180)) EGPrefs::put("webapi", "lon", lon);
    }
    EGPrefs::commit();
    saved = true;
  }

  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("ESPGeiger Network"));
  if (saved) {
    res.sendChunk(F("<div class=card style='border-color:#5a5'>Saved.</div>"));
  }
  res.sendChunk(F(
    "<p class=muted>ESPGeiger can share data with the public station "
    "network so it appears on the global map. The device identity and "
    "location are obscured before publication.</p>"));

  uint8_t mode = (uint8_t)EGPrefs::getUInt("webapi", "mode");
  if (mode > 0) {
    if (webapi.getStationId() != 0) {
      char status[160];
      int n = snprintf_P(status, sizeof(status),
        PSTR("<p>Station: <a href='" WEBAPI_STATION_URL "' target=_blank>#%u</a></p>"),
        webapi.getStationId(), webapi.getStationId());
      if (n > 0) res.sendChunk(status, (size_t)n);
    } else {
      res.sendChunk(F(
        "<p>Station: not yet registered (handshake pending) "
        "<button type=button onclick='location.reload()'>Refresh</button></p>"));
    }
  }

  res.sendChunk(F(
    "<form method=POST action=/webapi><fieldset><legend>Sharing</legend>"
    "<label><input type=radio name=m value=0"));
  if (mode == 0) res.sendChunk(F(" checked"));
  res.sendChunk(F("> Off</label>"
    "<label><input type=radio name=m value=1"));
  if (mode == 1) res.sendChunk(F(" checked"));
  res.sendChunk(F("> Heartbeat</label>"
    "<label><input type=radio name=m value=2"));
  if (mode == 2) res.sendChunk(F(" checked"));
  res.sendKV(F("> CPM Readings</label></fieldset>"
               "<label for=lat>Latitude</label>"
               "<input id=lat name=lat type=number step=any min=-90 max=90 placeholder='approximate by IP if blank' value='"),
             EGPrefs::getString("webapi", "lat"),
             F("'><label for=lon>Longitude</label>"
               "<input id=lon name=lon type=number step=any min=-180 max=180 placeholder='approximate by IP if blank' value='"));
  res.sendKV(nullptr, EGPrefs::getString("webapi", "lon"),
             F("'><p style=margin-top:.5em>"));
  res.sendChunk(F(
    "<button type=button onclick=\"window.open('https://loc.espgeiger.com/','espgeiger-loc')\">Find location</button></p>"
    "<button type=submit>Save</button></form>"
    "<script>window.addEventListener('message',function(e){if(e.origin!=='https://loc.espgeiger.com')return;var d=e.data;if(!d||d.type!=='espgeiger-location')return;var la=document.getElementById('lat');var lo=document.getElementById('lon');if(la&&typeof d.lat==='number')la.value=d.lat;if(lo&&typeof d.lon==='number')lo.value=d.lon;});</script>"));

  res.sendChunk(F(
    "<details style=margin-top:1em><summary>Advanced</summary>"
    "<p class=muted>Reset the device key. The next handshake registers a new station &mdash; the existing one <b>cannot be retrieved</b>.</p>"
    "<form method=POST action=/webapikeyreset onsubmit=\"return confirm('Reset the ESPGeiger Network key? This orphans the existing station.')\">"
    "<button type=submit class=danger>Reset key</button></form>"));
  if (mode > 0 && webapi.getStationId() != 0) {
    res.sendChunk(F(
      "<p class=muted style=margin-top:1em>Delete this station from the network. Removes all readings and history server-side and switches sharing to Off. <b>Cannot be undone.</b></p>"
      "<form method=POST action=/webapiforget onsubmit=\"return confirm('Permanently delete this station from the network? This cannot be undone.')\">"
      "<button type=submit class=danger>Forget station</button></form>"));
  }
  res.sendChunk(F("</details>"));

  WebPortal::sendPageTail(res);
  res.endChunked();
}

static void hKeyReset(EGHttpRequest& req, EGHttpResponse& res, void*) {
  Log::console(PSTR("WebAPI: Resetting api.key"));
  LittleFS.begin();
  LittleFS.remove("/api.key");
  LittleFS.end();
  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("Key Reset"));
  res.sendChunk(F("<p>ESPGeiger Network key cleared. Device is restarting; a new station identity will be created on next connection.</p>"));
  WebPortal::sendRestartCountdown(res);
  WebPortal::sendPageTail(res);
  res.endChunked();
  WebPortal::requestRestart(1500);
}

static void hForget(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // Sign before flipping mode so the request still has a valid key + id.
  webapi.forget();
  EGPrefs::put("webapi", "mode", "0");
  EGPrefs::commit();

  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("Station Forgotten"));
  res.sendChunk(FPSTR(FORGET_BODY));
  WebPortal::sendPageTail(res);
  res.endChunked();
}

static const EGMenuEntry WEBAPI_MENU[] = {
  {"/webapi", "ESPGeiger Network"},
  {nullptr, nullptr}
};
const EGMenuEntry* WebAPI::menuEntries() { return WEBAPI_MENU; }

void WebAPI::registerRoutes(EGHttpServer& http) {
  http.on("/webapi",         EGHttpRequest::GET,  hWebAPI);
  http.on("/webapi",         EGHttpRequest::POST, hWebAPI);
  http.on("/webapikeyreset", EGHttpRequest::POST, hKeyReset);
  http.on("/webapiforget",   EGHttpRequest::POST, hForget);
}
#endif
