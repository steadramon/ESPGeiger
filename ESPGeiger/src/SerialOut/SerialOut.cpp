/*
  SerialOut.cpp - Serial Out class
  
  Copyright (C) 2023 @steadramon

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
#ifdef SERIALOUT

#include <Arduino.h>
#include <EGHttpServer.h>
#include "SerialOut.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Prefs/EGPrefs.h"
#include "../Util/StringUtil.h"
#include "../Util/MathUtil.h"
#ifdef ESPG_HV_ADC
#include "../HV/HV.h"
#endif

SerialOut serialout;
EG_REGISTER_MODULE(serialout)

// State persisted across reboots so the user's last `show` selection
// survives a restart. Hidden from /param - this group is command-driven.
static const EGPref SOUT_PREF_ITEMS[] = {
  {"interval", nullptr, nullptr, "0", nullptr, 0, 65535, 0, EGP_UINT, EGP_HIDDEN},
  {"flags",    nullptr, nullptr, "0", nullptr, 0, 255,   0, EGP_UINT, EGP_HIDDEN},
};
static const EGPrefGroup SOUT_PREF_GROUP = {
  "sout", "Serial out", 1,
  SOUT_PREF_ITEMS,
  sizeof(SOUT_PREF_ITEMS) / sizeof(SOUT_PREF_ITEMS[0]),
};

const EGPrefGroup* SerialOut::prefs_group() { return &SOUT_PREF_GROUP; }

void SerialOut::on_prefs_loaded() {
  _interval   = (uint16_t)EGPrefs::getUInt("sout", "interval");
  _show_flags = (uint8_t)EGPrefs::getUInt("sout", "flags");
  Log::setSerialLogLevel(_interval == 0);
}

void SerialOut::save() {
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", (unsigned)_interval);
  EGPrefs::put("sout", "interval", buf);
  snprintf(buf, sizeof(buf), "%u", (unsigned)_show_flags);
  EGPrefs::put("sout", "flags", buf);
  EGPrefs::commit();
}

SerialOut::SerialOut() {
}

void SerialOut::print_cpm() {
  char buf[32];
  snprintf_P(buf, sizeof(buf), PSTR("CPM: %d\n"), gcounter.get_cpm());
  Serial.print(buf);
}

void SerialOut::print_usv() {
  char buf[32];
  char f[12];
  format_f(f, sizeof(f), gcounter.get_usv());
  snprintf_P(buf, sizeof(buf), PSTR("uSv: %s\n"), f);
  Serial.print(buf);
}

void SerialOut::set_show(int var) {
  _interval = (uint16_t)clamp(var, 0, (int)UINT16_MAX);
  // Preserve _show_flags across off->on transitions so `show 0` then bare
  // `show` restores the last selection. Default to CPM only when nothing
  // is remembered.
  if (_interval > 0 && _show_flags == 0) {
    _show_flags = SHOW_CPM;
  }
  Log::setSerialLogLevel(_interval == 0);
  save();
}

void SerialOut::setInterval(uint16_t v) {
  _interval = v;
  save();
}

void SerialOut::toggle_cpm() { _show_flags ^= SHOW_CPM; Log::setSerialLogLevel(_show_flags == 0); save(); }
void SerialOut::toggle_usv() { _show_flags ^= SHOW_USV; Log::setSerialLogLevel(_show_flags == 0); save(); }
void SerialOut::toggle_hv()  { _show_flags ^= SHOW_HV;  Log::setSerialLogLevel(_show_flags == 0); save(); }
void SerialOut::toggle_cps() { _show_flags ^= SHOW_CPS; Log::setSerialLogLevel(_show_flags == 0); save(); }

void SerialOut::loop(unsigned long now) {
  if (_show_flags == 0) return;
  if (_interval == 0) return;
  if ((now - _last_fire) < (uint32_t)_interval * 1000UL) return;
  _last_fire = now;

  char buf[80];
  char f[12];
  size_t pos = 0;
  int n;
  if (_show_flags & SHOW_CPM) {
    n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR("CPM: %d"), gcounter.get_cpm());
    advance_pos(pos, n, sizeof(buf));
  }
  if (_show_flags & SHOW_CPS) {
    format_f(f, sizeof(f), gcounter.get_cps());
    n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR("%sCPS: %s"), pos ? " " : "", f);
    advance_pos(pos, n, sizeof(buf));
  }
  if (_show_flags & SHOW_USV) {
    format_f(f, sizeof(f), gcounter.get_usv());
    n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR("%suSv: %s"), pos ? " " : "", f);
    advance_pos(pos, n, sizeof(buf));
  }
#ifdef ESPG_HV_ADC
  if (_show_flags & SHOW_HV) {
    n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR("%sHV: %d"), pos ? " " : "", (int)hv.hvReading.get());
    advance_pos(pos, n, sizeof(buf));
  }
#endif
  if (pos == 0) return;
  if (pos < sizeof(buf) - 1) buf[pos++] = '\n';
  Serial.write((const uint8_t*)buf, pos);
}

// ---------- HTTP routes ----------

static void hSerial(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // Toggle: off -> on (silence logger so SerialOut stream stays clean);
  // on -> off (re-enable logger).
  if (serialout.interval() == 0) {
    serialout.setInterval(1);
    Log::setSerialLogLevel(false);
  } else {
    serialout.setInterval(0);
    Log::setSerialLogLevel(true);
  }
  res.send(200, "text/plain", "OK");
}

void SerialOut::registerRoutes(EGHttpServer& http) {
  http.on("/serial", EGHttpRequest::GET, hSerial);
}
#endif
