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
#include <EGFormat.h>
#include <EGHttpServer.h>
#include "SerialOut.h"
#include "GeigerInput/SerialFormat.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/Util/Words.h"
#include "Corium/Util/MathUtil.h"
#ifdef ESPG_HV_ADC
#include "HV/HV.h"
#endif

EG_PSTR(K_FLAGS,  "flags");
EG_PSTR(K_FORMAT, "format");
EG_PSTR(K_BAUD,   "baud");

#define SOUT_TPL_MAX 128

SerialOut serialout;
EG_REGISTER_MODULE(serialout)

EG_PSTR(SO_L_FMT,  "Wire format");
EG_PSTR(SO_O_FMT,  "Labelled|MightyOhm|User template");
EG_PSTR(SO_L_TPL,  "Template");
EG_PSTR(SO_H_TPL,  "User template. See /vars.json for live values. Escapes: \\n \\r \\t");
EG_PSTR(SO_L_BAUD, "Baud override");
EG_PSTR(SO_H_BAUD, "0=auto (format native). Non-zero overrides.");

static const EGPref SOUT_PREF_ITEMS[] PROGMEM = {
  {EG_K_INTERVAL, nullptr, nullptr,  "0",  nullptr, 0, 65535,  0,   EGP_UINT,   EGP_HIDDEN},
  {K_FLAGS,       nullptr, nullptr,  "0",  nullptr, 0, 255,    0,   EGP_UINT,   EGP_HIDDEN},
  {K_FORMAT,      SO_L_FMT,  nullptr,   "0",  SO_O_FMT, 0, 2,     0,   EGP_ENUM,   0},
  {K_BAUD,        SO_L_BAUD, SO_H_BAUD, "0",  nullptr, 0, 921600, 0,   EGP_UINT,   EGP_ADVANCED},
  {EG_K_TPL,      SO_L_TPL,  SO_H_TPL,  "{cpm}\\n", nullptr, 0, 0,    SOUT_TPL_MAX, EGP_STRING, 0},
};
static const EGPrefGroup SOUT_PREF_GROUP PROGMEM = {
  "sout", "Serial out", 1,
  SOUT_PREF_ITEMS,
  sizeof(SOUT_PREF_ITEMS) / sizeof(SOUT_PREF_ITEMS[0]),
  EGP_CAT_SYSTEM,
};

const EGPrefGroup* SerialOut::prefs_group() { return &SOUT_PREF_GROUP; }

void SerialOut::on_prefs_loaded() {
  _interval   = (uint16_t)EGPrefs::getUInt("sout", EG_K_INTERVAL);
  _show_flags = (uint8_t)EGPrefs::getUInt("sout", K_FLAGS);
  _format     = (uint8_t)EGPrefs::getUInt("sout", K_FORMAT);
  _baud       = (uint32_t)EGPrefs::getUInt("sout", K_BAUD);
  Log::setSerialLogLevel(_interval == 0);
  EGModuleRegistry::set_loop_interval(this, _interval > 0 ? 500 : -1);
  resolveFormat();
  applyBaud();
}

// not SerialFormat's input IDs
static SerialFormat::FormatFn sout_resolve(uint8_t fmt) {
  switch (fmt) {
    case 1:  return SerialFormat::fmt_mightyohm;
    case 2:  return SerialFormat::fmt_user_template;
    default: return nullptr;       // 0 = Labelled
  }
}

static uint32_t sout_baud(uint8_t fmt) {
  switch (fmt) {
    case 1:  return 9600;
    default: return 0;
  }
}

void SerialOut::resolveFormat() {
  _fmt_fn = sout_resolve(_format);
}

void SerialOut::save() {
  EGPrefs::putUInt("sout", EG_K_INTERVAL, _interval);
  EGPrefs::putUInt("sout", K_FLAGS, _show_flags);
  EGPrefs::putUInt("sout", K_FORMAT, _format);
  EGPrefs::putUInt("sout", K_BAUD, _baud);
  EGPrefs::commit();
}

void SerialOut::applyBaud() {
  uint32_t target = _baud;
  if (target == 0 && _format != 0) target = sout_baud(_format);
  if (target == 0) return;
  Serial.flush();
#if !defined(ARDUINO_USB_CDC_ON_BOOT) || ARDUINO_USB_CDC_ON_BOOT == 0
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
  EGModuleRegistry::set_loop_interval(this, 100);
  // keep flags for a bare show
  if (_interval > 0 && _show_flags == 0) {
    _show_flags = SHOW_CPM;
  }
  Log::setSerialLogLevel(_interval == 0);
  save();
}

void SerialOut::setInterval(uint16_t v) {
  _interval = v;
  EGModuleRegistry::set_loop_interval(this, 100);
  save();
}

void SerialOut::toggle_cpm() { _show_flags ^= SHOW_CPM; Log::setSerialLogLevel(_show_flags == 0); save(); }
void SerialOut::toggle_usv() { _show_flags ^= SHOW_USV; Log::setSerialLogLevel(_show_flags == 0); save(); }
void SerialOut::toggle_hv()  { _show_flags ^= SHOW_HV;  Log::setSerialLogLevel(_show_flags == 0); save(); }
void SerialOut::toggle_cps() { _show_flags ^= SHOW_CPS; Log::setSerialLogLevel(_show_flags == 0); save(); }

struct Line {
  uint8_t flags;
  int cpm;
  float cps, usv;
  int hv;
};

static size_t labelled_line(char* buf, size_t cap, const Line& v) {
  static const char SP[] PROGMEM = " ";
  char f[12];
  size_t pos = 0;
  int n;
  if (v.flags & SerialOut::SHOW_CPM) {
    n = snprintf_P(buf + pos, cap - pos, PSTR("CPM: %d"), v.cpm);
    advance_pos(pos, n, cap);
  }
  if (v.flags & SerialOut::SHOW_CPS) {
    format_f(f, sizeof(f), v.cps);
    n = snprintf_P(buf + pos, cap - pos, PSTR("%sCPS: %s"), pos ? SP : PSTR(""), f);
    advance_pos(pos, n, cap);
  }
  if (v.flags & SerialOut::SHOW_USV) {
    format_f(f, sizeof(f), v.usv);
    n = snprintf_P(buf + pos, cap - pos, PSTR("%suSv: %s"), pos ? SP : PSTR(""), f);
    advance_pos(pos, n, cap);
  }
#ifdef ESPG_HV_ADC
  if (v.flags & SerialOut::SHOW_HV) {
    n = snprintf_P(buf + pos, cap - pos, PSTR("%sHV: %d"), pos ? SP : PSTR(""), v.hv);
    advance_pos(pos, n, cap);
  }
#endif
  return pos;
}

void SerialOut::loop(unsigned long now) {
  if (_interval == 0) {
    EGModuleRegistry::set_loop_interval(this, -1);
    return;
  }
  uint32_t period = (uint32_t)_interval * 1000UL;
  if ((now - _last_fire) < period) {
    EGModuleRegistry::sleep_until(this, now, _last_fire + period);
    return;
  }
  _last_fire = now;
  EGModuleRegistry::sleep_until(this, now, now + period);

  if (_fmt_fn) {
    char line[SOUT_TPL_MAX * 2];  // vars grow it, cut cleanly past this
    size_t n = _fmt_fn(line, sizeof(line));
    if (n) Serial.write((const uint8_t*)line, n);
    return;
  }

  if (_show_flags == 0) return;

  Line v = { _show_flags, gcounter.get_cpm(), gcounter.get_cps(), gcounter.get_usv(), 0 };
#ifdef ESPG_HV_ADC
  v.hv = (int)hv.hvReading.get();
#endif
  char buf[80];
  size_t pos = labelled_line(buf, sizeof(buf), v);
  if (pos == 0) return;
  if (pos < sizeof(buf) - 1) buf[pos++] = '\n';
  Serial.write((const uint8_t*)buf, pos);
}

// HTTP routes

static void hSerial(EGHttpRequest& req, EGHttpResponse& res, void*) {
  if (serialout.interval() == 0) {
    serialout.setInterval(1);
    Log::setSerialLogLevel(false);
  } else {
    serialout.setInterval(0);
    Log::setSerialLogLevel(true);
  }
  res.send(200, FPSTR(HTTP_CT_PLAIN), FPSTR(W_OK_TEXT));
}

void SerialOut::registerRoutes(EGHttpServer& http) {
  http.on(F("/serial"), EGHttpRequest::GET, hSerial);
}
#endif
