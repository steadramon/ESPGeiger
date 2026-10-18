/*
  prod_webapi.cpp - the real WebAPI.cpp, built against wapi_fakes.h and a
  settable time().

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
#include "wapi_fakes.h"
#define time(out) fake_time(out)
time_t fake_time(time_t* out);
#define private public   // hs_reply sets what was sent
#include "WebAPI/WebAPI.cpp"
#undef private

time_t fake_now = 0;
time_t fake_time(time_t* out) { if (out) *out = fake_now; return fake_now; }

// 0 on overflow
size_t post_body(uint8_t* buf, size_t cap, uint32_t id, bool census, bool health) {
  MsgPack::Writer mp(buf, cap);
  write_post(mp, id, census, health);
  return mp.overflow ? 0 : mp.length();
}

bool webapi_denied(EGHttpClient* r) { return denied(r); }

uint32_t first_wake(uint32_t now) {
  WebAPI w;
  w.loop(now);
  return wapi.wake;
}

uint32_t hs_reply(uint32_t id, bool arrived) {
  WebAPI w;
  w._sent = WebAPI::SENT_HS;
  uint8_t r[16];
  MsgPack::Writer mp(r, sizeof(r));
  mp.map(1);
  mp.kv(PSTR("id"), id);
  w.request.reply.assign((const char*)r, mp.length());
  w.request.done = arrived;
  w.loop(1000);
  return w.getStationId();
}

// forget, then sharing off
bool forget_off(void) {
  WebAPI w;
  w.station_id = 77;
  w._have_key = true;
  w.forget();
  wapi.mode = 0;
  w.on_prefs_loaded();
  if (wapi.interval == -1) return false;
  w.loop(1000);
  if (w.request.url.find("/forget") == std::string::npos) return false;
  if (wapi.interval == -1) return false;   // reply still due
  w.request.done = true;
  w.loop(2000);
  return w.station_id == 0 && wapi.interval == -1;
}

bool key_kept(const char* file) {
  LittleFS.files.clear();
  LittleFS.files["/api.key"] = file;
  WebAPI w;
  w.begin();
  if (!w._have_key) return false;
  uint8_t was[WEBAPI_PRIV_LEN];
  memcpy(was, w.priv_k, sizeof(was));
  w.doHandshake();
  return memcmp(was, w.priv_k, sizeof(was)) == 0;
}

// moved to api.bad as is
bool key_refused(const char* file) {
  LittleFS.files.clear();
  LittleFS.files["/api.key"] = file;
  WebAPI w;
  w.begin();
  if (w._have_key || LittleFS.exists("/api.key")) return false;
  return LittleFS.exists("/api.bad") && LittleFS.files["/api.bad"] == file;
}

bool hs_refused(void) {
  LittleFS.files.clear();
  LittleFS.files["/api.key"] = "111111111111111111111111111111111111111111111111\r\n";
  WebAPI w;
  w.begin();
  w.take_publish_err();
  w.request.refuse = true;
  w.doHandshake();
  return w.take_publish_err() == 1 && !w.last_ok && w._sent == WebAPI::SENT_NONE;
}

size_t handshake_body(uint8_t* buf, size_t cap, const char* pubKey, uint8_t mode, bool crash) {
  HsCrash c;
  if (crash) collect_crash(c);
  MsgPack::Writer mp(buf, cap);
  write_handshake(mp, pubKey, mode, c);
  return mp.overflow ? 0 : mp.length();
}
