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
#include "../GeigerInput/SerialFormat.h"
#include "../GeigerInput/GeigerInput.h"   // GEIGER_IS_SERIAL macro
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

EG_PSTR(SO_L_FMT,  "Wire format");
#if GEIGER_IS_SERIAL(GEIGER_TYPE)
EG_PSTR(SO_H_FMT,  "0=Labelled (default), 4=ESPGeiger, 5=User template, 1=GC10, 2=GC10Next, 3=MightyOhm");
#else
EG_PSTR(SO_H_FMT,  "0=Labelled (default), 4=ESPGeiger, 5=User template, 3=MightyOhm");
#endif
EG_PSTR(SO_L_TPL,  "Template");
EG_PSTR(SO_H_TPL,  "format=5 only. See /vars.json for live values. Escapes: \\n \\r \\t");
EG_PSTR(SO_L_BAUD, "Baud override");
EG_PSTR(SO_H_BAUD, "0=auto (format native). Non-zero overrides.");

// interval / flags stay hidden - they're driven by `show N` / `show cpm`
// commands. The format and template fields are user-facing.
static const EGPref SOUT_PREF_ITEMS[] = {
  {"interval", nullptr, nullptr,  "0",  nullptr, 0, 65535,  0,   EGP_UINT,   EGP_HIDDEN},
  {"flags",    nullptr, nullptr,  "0",  nullptr, 0, 255,    0,   EGP_UINT,   EGP_HIDDEN},
  {"format",   SO_L_FMT,  SO_H_FMT,  "0",  nullptr, 0, 5,      0,   EGP_UINT,   0},
  {"baud",     SO_L_BAUD, SO_H_BAUD, "0",  nullptr, 0, 921600, 0,   EGP_UINT,   EGP_ADVANCED},
  {"tpl",      SO_L_TPL,  SO_H_TPL,  "",   nullptr, 0, 0,      128, EGP_STRING, 0},
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
  _format     = (uint8_t)EGPrefs::getUInt("sout", "format");
  _baud       = (uint32_t)EGPrefs::getUInt("sout", "baud");
  Log::setSerialLogLevel(_interval == 0);
  resolveFormat();
  applyBaud();
}

void SerialOut::resolveFormat() {
  _fmt_fn = SerialFormat::resolve(_format);
}

void SerialOut::save() {
  char buf[12];
  snprintf(buf, sizeof(buf), "%u", (unsigned)_interval);
  EGPrefs::put("sout", "interval", buf);
  snprintf(buf, sizeof(buf), "%u", (unsigned)_show_flags);
  EGPrefs::put("sout", "flags", buf);
  snprintf(buf, sizeof(buf), "%u", (unsigned)_format);
  EGPrefs::put("sout", "format", buf);
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)_baud);
  EGPrefs::put("sout", "baud", buf);
  EGPrefs::commit();
}

void SerialOut::applyBaud() {
  // Resolution: explicit _baud wins; else if a protocol format is set use
  // its native baud; else keep whatever framework already configured.
  uint32_t target = _baud;
  if (target == 0 && _format != 0) target = SerialFormat::baud_for(_format);
  if (target == 0) return;
  Serial.flush();
#if !defined(ARDUINO_USB_CDC_ON_BOOT) || ARDUINO_USB_CDC_ON_BOOT == 0
  // USB-CDC has no baud; updateBaudRate only exists on HardwareSerial.
  Serial.updateBaudRate(target);
#endif
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

void SerialOut::setFormat(uint8_t f) {
  _format = f;
  resolveFormat();
  save();
  applyBaud();
}

void SerialOut::setBaud(uint32_t b) {
  _baud = b;
  save();
  applyBaud();
}

void SerialOut::toggle_cpm() { _show_flags ^= SHOW_CPM; Log::setSerialLogLevel(_show_flags == 0); save(); }
void SerialOut::toggle_usv() { _show_flags ^= SHOW_USV; Log::setSerialLogLevel(_show_flags == 0); save(); }
void SerialOut::toggle_hv()  { _show_flags ^= SHOW_HV;  Log::setSerialLogLevel(_show_flags == 0); save(); }
void SerialOut::toggle_cps() { _show_flags ^= SHOW_CPS; Log::setSerialLogLevel(_show_flags == 0); save(); }

void SerialOut::loop(unsigned long now) {
  if (_interval == 0) return;
  if ((now - _last_fire) < (uint32_t)_interval * 1000UL) return;
  _last_fire = now;

  // Protocol-format mode: cached formatter pointer, no per-emit dispatch.
  // Custom formatters (MightyOhm, user-template) pull live values themselves;
  // fixed-template protocols (GC10/Next/ESPGeiger) go through OutputVars.
  if (_fmt_fn) {
    char line[128];  // user template can be up to ~96 B after substitution
    size_t n = _fmt_fn(line, sizeof(line));
    if (n) Serial.write((const uint8_t*)line, n);
    return;
  }

  if (_show_flags == 0) return;

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
