/*
  URADMonitor.cpp - uRADMonitor open-data upload client

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
#ifdef URADMONITOROUT
#include "URADMonitor.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../NTP/NTP.h"
#ifdef ESPG_HV_ADC
#include "../ESPGHW/ESPGHW.h"
#endif

extern uint8_t send_indicator;

URADMonitor uradmon;
EG_REGISTER_MODULE(uradmon)

static const EGPref URADMON_PREF_ITEMS[] = {
  {"send",      "Enable",     "Upload to uRADMonitor",                     "0",        nullptr, 0, 0,  0,  EGP_BOOL,   0},
  {"user_id",   "User ID",    "uRADMonitor account ID",                    "",         nullptr, 0, 0, 16, EGP_STRING, 0},
  {"user_hash", "User Hash",  "uRADMonitor account key",                   "",         nullptr, 0, 0, 64, EGP_STRING, EGP_SENSITIVE},
  {"device_id", "Device ID",  "8 hex chars; 00000000 = request from server","00000000", "[0-9A-Fa-f]{8}", 0, 0, 8, EGP_STRING, 0},
  {"interval",  "Interval",   "Upload interval (sec)",                     "60",       nullptr, URADMON_INTERVAL_MIN, URADMON_INTERVAL_MAX, 0, EGP_UINT, 0},
};

static const EGPrefGroup URADMON_PREF_GROUP = {
  "uradmon", "uRADMonitor", 1,
  URADMON_PREF_ITEMS,
  sizeof(URADMON_PREF_ITEMS) / sizeof(URADMON_PREF_ITEMS[0]),
};

const EGPrefGroup* URADMonitor::prefs_group() { return &URADMON_PREF_GROUP; }

size_t URADMonitor::status_json(char* buf, size_t cap, unsigned long now) {
  if (!EGPrefs::getBool("uradmon", "send")) return 0;
  return write_status_json(buf, cap, "uradmon", last_ok, last_attempt_ms, now);
}

URADMonitor::URADMonitor() {
}

void URADMonitor::setInterval(int interval) {
  if (interval == pingInterval) return;
  if (interval > URADMON_INTERVAL_MAX) interval = URADMON_INTERVAL_MAX;
  if (interval < URADMON_INTERVAL_MIN) interval = URADMON_INTERVAL_MIN;
  pingInterval = interval;
  pingIntervalMs = (uint32_t)interval * 1000UL;
}

int URADMonitor::getInterval() {
  return pingInterval;
}

void URADMonitor::loop(unsigned long now) {
  if (lastPing == 0) {
    int rtimer = (int)EGPrefs::getUInt("uradmon", "interval");
    if (rtimer == 0) rtimer = URADMON_INTERVAL;
    setInterval(rtimer);
    lastPing = now + random(pingIntervalMs);
    return;
  }
  if (now > lastPing && (now - lastPing) >= pingIntervalMs) {
    lastPing += pingIntervalMs;
    if ((now - lastPing) >= pingIntervalMs) lastPing = now + random(pingIntervalMs);
    postMeasurement();
  }
}

void URADMonitor::httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState) {
  if (readyState != readyStateDone) return;
  URADMonitor* self = static_cast<URADMonitor*>(optParm);
  self->last_attempt_ms = millis();
  self->last_ok = false;
  if (request->responseHTTPcode() == 200) {
    String r = request->responseText();
    // DIDAP response: {"setid": "131234AB"}
    int p = r.indexOf("setid");
    if (p >= 0) {
      int q = r.indexOf('"', p + 6);
      int e = (q >= 0) ? r.indexOf('"', q + 1) : -1;
      if (q >= 0 && e > q && (e - q - 1) == 8) {
        char newid[9];
        strncpy(newid, r.c_str() + q + 1, 8);
        newid[8] = '\0';
        if (EGPrefs::put("uradmon", "device_id", newid)) {
          EGPrefs::commit();
          Log::console(PSTR("uRADMonitor: Device ID assigned: %s"), newid);
        }
      }
    }
    Log::debug(PSTR("uRADMonitor: Upload OK"));
    self->last_ok = true;
  } else {
    Log::console(PSTR("uRADMonitor: Error %d - %s"),
      request->responseHTTPcode(), request->responseHTTPString().c_str());
  }
  self->note_publish(self->last_ok);
}

void URADMonitor::postMeasurement() {
  if (!gcounter.is_warm()) return;
  if (!EGPrefs::getBool("uradmon", "send")) return;
  if (!ntpclient.synced) {
    Log::debug(PSTR("uRADMonitor: NTP not synced, skipping"));
    return;
  }

  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("uRADMonitor: Testmode"));
    return;
  }

  const char* user_id   = EGPrefs::getString("uradmon", "user_id");
  const char* user_hash = EGPrefs::getString("uradmon", "user_hash");
  const char* device_id = EGPrefs::getString("uradmon", "device_id");
  if (user_id[0] == '\0' || user_hash[0] == '\0') {
    Log::console(PSTR("uRADMonitor: Skipping upload, please set User ID and User Hash"));
    return;
  }
  if (device_id[0] == '\0') device_id = "00000000";

  int rtimer = (int)EGPrefs::getUInt("uradmon", "interval");
  if (rtimer == 0) rtimer = URADMON_INTERVAL;
  setInterval(rtimer);

  int avgcpm;
  if (rtimer <= 90) {
    avgcpm = gcounter.get_cpm();
  } else if (rtimer <= 450) {
    avgcpm = gcounter.get_cpm5();
  } else {
    avgcpm = gcounter.get_cpm15();
  }

  // Compact body: ID/value/ID/value
  // 01 = unix timestamp (required)
  // 0B = radiation CPM
  // 0C = inverter voltage (V) — espgeigerhw with HV ADC only
  // 0D = inverter duty (per-mille, 0-1000) — espgeigerhw only
  char body[128];
  size_t pos = 0;
  int n = snprintf_P(body + pos, sizeof(body) - pos,
    PSTR("01/%lu/0B/%d"), (unsigned long)time(NULL), avgcpm);
  if (n > 0) pos += n;
#ifdef ESPG_HV_ADC
  if (pos < sizeof(body)) {
    n = snprintf_P(body + pos, sizeof(body) - pos,
      PSTR("/0C/%.1f"), hardware.hvReading.get());
    if (n > 0) pos += n;
  }
#endif
#ifdef ESPG_HV
  if (pos < sizeof(body)) {
    // duty is 0-255 on device; uRADMonitor wants per-mille
    n = snprintf_P(body + pos, sizeof(body) - pos,
      PSTR("/0D/%d"), (hardware.get_duty() * 1000) / 255);
    if (n > 0) pos += n;
  }
#endif

  Log::debug(PSTR("uRADMonitor: POST device=%s body=%s"), device_id, body);

  if (request.readyState() == readyStateUnsent || request.readyState() == readyStateDone) {
    if (request.open("POST", URADMON_URL)) {
      led.Blink(500, 500);
      request.setReqHeader(F("User-Agent"), DeviceInfo::useragent());
      request.setReqHeader(F("Content-Type"), F("text/plain"));
      request.setReqHeader(F("X-User-id"),   user_id);
      request.setReqHeader(F("X-User-hash"), user_hash);
      request.setReqHeader(F("X-Device-id"), device_id);
      request.onReadyStateChange(httpRequestCb, this);
      request.setTimeout(30);
      request.send(body);
      send_indicator = 2;
    } else {
      Log::console(PSTR("uRADMonitor: Can't send request"));
    }
  }
}
#endif
