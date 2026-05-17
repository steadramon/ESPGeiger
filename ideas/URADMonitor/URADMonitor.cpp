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
#include "../HV/HV.h"
extern HV hv;
#endif

extern uint8_t send_indicator;

URADMonitor uradmon;
EG_REGISTER_MODULE(uradmon)

EG_PSTR(UR_L_EN,  "Enable");
EG_PSTR(UR_H_EN,  "Upload to uRADMonitor");
EG_PSTR(UR_L_UID, "User ID");
EG_PSTR(UR_H_UID, "uRADMonitor account ID");
EG_PSTR(UR_L_KEY, "User Hash");
EG_PSTR(UR_H_KEY, "uRADMonitor account key");
EG_PSTR(UR_L_DID, "Device ID");
EG_PSTR(UR_H_DID, "Leave 0 to auto-assign under ESPGeiger class (13), or paste an existing 8-hex ID");
EG_PSTR(UR_P_DID, "0|[0-9A-Fa-f]{8}");
EG_PSTR(UR_L_INT, "Interval");
EG_PSTR(UR_H_INT, "Upload interval (sec)");

static const EGPref URADMON_PREF_ITEMS[] = {
  {"send",      UR_L_EN,  UR_H_EN,  "0",        nullptr, 0, 0,  0, EGP_BOOL,   0},
  {"user_id",   UR_L_UID, UR_H_UID, "",         nullptr, 0, 0, 16, EGP_STRING, 0},
  {"user_hash", UR_L_KEY, UR_H_KEY, "",         nullptr, 0, 0, 64, EGP_STRING, EGP_SENSITIVE},
  {"device_id", UR_L_DID, UR_H_DID, "0",        UR_P_DID, 0, 0, 8, EGP_STRING, 0},
  {"interval",  UR_L_INT, UR_H_INT, "60",       nullptr, URADMON_INTERVAL_MIN, URADMON_INTERVAL_MAX, 0, EGP_UINT, 0},
};

static const EGPrefGroup URADMON_PREF_GROUP = {
  "uradmon", "uRADMonitor", 1,
  URADMON_PREF_ITEMS,
  sizeof(URADMON_PREF_ITEMS) / sizeof(URADMON_PREF_ITEMS[0]),
};

const EGPrefGroup* URADMonitor::prefs_group() { return &URADMON_PREF_GROUP; }

size_t URADMonitor::status_json(char* buf, size_t cap, unsigned long now) {
  if (!_send_enabled) return 0;
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

void URADMonitor::on_prefs_loaded() {
  int rtimer = (int)EGPrefs::getUInt("uradmon", "interval");
  if (rtimer == 0) rtimer = URADMON_INTERVAL;
  setInterval(rtimer);
  _send_enabled = EGPrefs::getBool("uradmon", "send");
  EGModuleRegistry::set_loop_interval(this, _send_enabled ? 500 : 60000);
}

void URADMonitor::loop(unsigned long now) {
  if (!_send_enabled) return;
  if (lastPing == 0) {
    lastPing = now - pingIntervalMs + random(pingIntervalMs);
  } else if ((now - lastPing) >= pingIntervalMs) {
    lastPing += pingIntervalMs;
    if ((now - lastPing) >= pingIntervalMs) lastPing = now;
    postMeasurement();
  }
  EGModuleRegistry::sleep_until(this, now, lastPing + pingIntervalMs);
}

void URADMonitor::httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState) {
  if (readyState != readyStateDone) return;
  URADMonitor* self = static_cast<URADMonitor*>(optParm);
  self->last_ok = false;
  if (request->responseHTTPcode() == 200) {
    // DIDAP: server hands back {"setid":"AABBCCDD"} on first upload with
    // an unassigned device_id. Persist it so future posts use the real ID.
    char r[64];
    size_t got = request->responseRead((uint8_t*)r, sizeof(r) - 1);
    r[got] = 0;
    const char* p = strstr(r, "setid");
    if (p) {
      const char* q = strchr(p + 5, '"');
      if (q) {
        const char* e = strchr(q + 1, '"');
        if (e && (e - q - 1) == 8) {
          char newid[9];
          memcpy(newid, q + 1, 8);
          newid[8] = '\0';
          if (EGPrefs::put("uradmon", "device_id", newid)) {
            EGPrefs::commit();
            Log::console(PSTR("uRADMonitor: Device ID assigned: %s"), newid);
          }
        }
      }
    }
    Log::debug(PSTR("uRADMonitor: Upload OK"));
    self->last_ok = true;
  } else {
    Log::console(PSTR("uRADMonitor: Error %d - %s"),
      request->responseHTTPcode(), request->responseHTTPString().c_str());
  }
  self->note_result(self->last_ok);
}

void URADMonitor::postMeasurement() {
  if (!gcounter.is_warm()) return;

  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("uRADMonitor: Testmode"));
    return;
  }
  if (!ntpclient.synced) {
    Log::debug(PSTR("uRADMonitor: NTP not synced, skipping"));
    return;
  }

  const char* user_id   = EGPrefs::getString("uradmon", "user_id");
  const char* user_hash = EGPrefs::getString("uradmon", "user_hash");
  const char* device_id = EGPrefs::getString("uradmon", "device_id");
  if (user_id[0] == '\0' || user_hash[0] == '\0') {
    Log::console(PSTR("uRADMonitor: Skipping upload, please set User ID and User Hash"));
    return;
  }
  // "" or "0" -> ESPGeiger class prefix; server returns assigned setid on first call.
  if (device_id[0] == '\0' || (device_id[0] == '0' && device_id[1] == '\0')) {
    device_id = "13000000";
  }

  int avgcpm;
  if (pingInterval <= 90) {
    avgcpm = gcounter.get_cpm();
  } else if (pingInterval <= 450) {
    avgcpm = gcounter.get_cpm5();
  } else {
    avgcpm = gcounter.get_cpm15();
  }

  // Compact body: ID/value pairs. 01 = unix ts, 0B = radiation CPM,
  // 0C = HV voltage, 0D = HV duty per-mille (HW-only fields).
  char body[128];
  size_t pos = 0;
  int n = snprintf_P(body + pos, sizeof(body) - pos,
    PSTR("01/%lu/0B/%d"), (unsigned long)time(NULL), avgcpm);
  if (n > 0) pos += n;
#ifdef ESPG_HV_ADC
  if (pos < sizeof(body)) {
    n = snprintf_P(body + pos, sizeof(body) - pos,
      PSTR("/0C/%u"), (unsigned)(hv.hvReading.get() + 0.5f));
    if (n > 0) pos += n;
  }
  if (pos < sizeof(body)) {
    n = snprintf_P(body + pos, sizeof(body) - pos,
      PSTR("/0D/%u"), (unsigned)((hv.get_duty() * 1000L) / 1023));
    if (n > 0) pos += n;
  }
#endif

  Log::debug(PSTR("uRADMonitor: POST device=%s body=%s"), device_id, body);

  if (!request) request = new AsyncHTTPRequest();
  if (!request) { Log::console(PSTR("uRADMonitor: alloc failed")); return; }

  if (request->readyState() == readyStateUnsent || request->readyState() == readyStateDone) {
    if (request->open("POST", URADMON_URL)) {
      led.Blink(500, 500);
      request->setReqHeader(F("User-Agent"), DeviceInfo::useragent());
      request->setReqHeader(F("Content-Type"), F("text/plain"));
      request->setReqHeader(F("X-User-id"),   user_id);
      request->setReqHeader(F("X-User-hash"), user_hash);
      request->setReqHeader(F("X-Device-id"), device_id);
      request->onReadyStateChange(httpRequestCb, this);
      request->setTimeout(30);
      request->send(body);
      note_attempt();
      send_indicator = 2;
    } else {
      Log::console(PSTR("uRADMonitor: Can't send request"));
    }
  }
}
#endif
