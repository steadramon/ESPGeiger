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

#include <new>
#include <EGFormat.h>
#include <EGHttpServer.h>
#include "SerialOut.h"
#include "App/AppKeys.h"
#include "App/FeatureBits.h"
#include "App/MeasureIds.h"
#include "GeigerInput/SerialFormat.h"
#include "Corium/Console/Console.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Measure/EGMeasure.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/Util/Words.h"
#include "Corium/Util/MathUtil.h"

EG_PSTR(K_FLAGS,  "flags");
EG_PSTR(K_FORMAT, "format");
EG_PSTR(K_BAUD,   "baud");

#define SOUT_TPL_MAX 128
#define SOUT_BAUD_MIN 9600

SerialOut serialout;
EG_REGISTER_FEATURE(serialout, FEAT_SERIALOUT)

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
  if (_baud && _baud < SOUT_BAUD_MIN) _baud = SOUT_BAUD_MIN;
  Log::setSerialLogLevel(_interval == 0);
  EGModuleRegistry::every(this, _interval > 0 ? LOOP_MS : 0);
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
  if (!EGPrefs::commit_soon(fast_millis())) Log::console(PSTR("SerialOut: not saved"));
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
  snprintf_P(buf, sizeof(buf), PSTR("CPM: %d\n"), (int)egm_i(M_CPM));
  Serial.print(buf);
}

void SerialOut::print_usv() {
  char buf[32];
  char f[12];
  format_f(f, sizeof(f), egm_f(M_USV));
  snprintf_P(buf, sizeof(buf), PSTR("uSv: %s\n"), f);
  Serial.print(buf);
}

void SerialOut::set_show(int var) {
  _interval = (uint16_t)clamp(var, 0, (int)UINT16_MAX);
  EGModuleRegistry::due_in(this, REPLAN_MS);
  // keep flags for a bare show
  if (_interval > 0 && _show_flags == 0) {
    _show_flags = SHOW_CPM;
  }
  Log::setSerialLogLevel(_interval == 0);
  save();
}

void SerialOut::setInterval(uint16_t v) {
  _interval = v;
  EGModuleRegistry::due_in(this, REPLAN_MS);
  save();
}

void SerialOut::toggle(uint8_t show) { _show_flags ^= show; Log::setSerialLogLevel(_show_flags == 0); save(); }

// console

struct ShowWord {
  char word[4];
  uint8_t show;   // 0 = taken, does nothing
};
static const ShowWord SHOW_WORDS[] PROGMEM = {
  {"cpm", SerialOut::SHOW_CPM},
  {"usv", SerialOut::SHOW_USV},
  {"cps", SerialOut::SHOW_CPS},
  {"hv",  M_HAS_HV ? SerialOut::SHOW_HV : 0},
};

static bool show_toggle(const char* arg) {
  for (const ShowWord& w : SHOW_WORDS) {
    if (strcmp_P(arg, w.word) != 0) continue;
    uint8_t show = pgm_read_byte(&w.show);
    if (show) serialout.toggle(show);
    return true;
  }
  return false;
}

static void cmd_show() {
  char* arg = Console::next();
  if (arg == NULL) {
    // bare show toggles, on = 1 s
    serialout.set_show(serialout.interval() > 0 ? 0 : 1);
    return;
  }
  if (show_toggle(arg)) return;
  int32_t aNumber;
  if (strcmp_P(arg, PSTR("off")) == 0) serialout.set_show(0);
  else if (isDigit(arg[0]) && parse_i32(arg, &aNumber)) serialout.set_show((int)aNumber);
  else Log::console(PSTR("show: unknown arg '%s'"), arg);
}

static void cmd_cpm() { serialout.print_cpm(); }
static void cmd_usv() { serialout.print_usv(); }

EG_REGISTER_CMD(cpm, cmd_cpm)
EG_REGISTER_CMD(usv, cmd_usv)
EG_REGISTER_CMD(show, cmd_show)

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
  if (M_HAS_HV && (v.flags & SerialOut::SHOW_HV)) {
    n = snprintf_P(buf + pos, cap - pos, PSTR("%sHV: %d"), pos ? SP : PSTR(""), v.hv);
    advance_pos(pos, n, cap);
  }
  return pos;
}

// what fits the tx fifo now, the rest over the next passes
void SerialOut::send(const char* s, size_t n) {
  int room = Serial.availableForWrite();
  if (room < (int)n) _rest = new (std::nothrow) char[n - room];
  if (!_rest) {
    // fits, or no heap: waits as before
    Serial.write((const uint8_t*)s, n);
    return;
  }
  Serial.write((const uint8_t*)s, room);
  _rest_n = (uint16_t)(n - room);
  _rest_at = 0;
  memcpy(_rest, s + room, _rest_n);
  EGModuleRegistry::due_in(this, 0);
}

void SerialOut::drain(bool wait) {
  size_t n = _rest_n - _rest_at;
  int room = Serial.availableForWrite();
  if (!wait && room < (int)n) n = room;
  Serial.write((const uint8_t*)_rest + _rest_at, n);
  _rest_at += n;
  if (_rest_at < _rest_n) return;
  delete[] _rest;
  _rest = nullptr;
}

void SerialOut::loop(unsigned long now) {
  uint32_t period = (uint32_t)_interval * 1000UL;
  bool due = (now - _last_fire) >= period;
  // next line due or off: the rest now
  if (_rest) drain(due);
  if (_rest) {
    EGModuleRegistry::due_in(this, 0);
    return;
  }
  if (_interval == 0) {
    EGModuleRegistry::every(this, 0);
    return;
  }
  if (!due) {
    EGModuleRegistry::due_at(this, _last_fire + period);
    return;
  }
  _last_fire = now;
  EGModuleRegistry::due_at(this, now + period);

  if (_fmt_fn) {
    char line[SOUT_TPL_MAX * 2];  // vars grow it, cut cleanly past this
    size_t n = _fmt_fn(line, sizeof(line));
    if (n) send(line, n);
    return;
  }

  if (_show_flags == 0) return;

  Line v = { _show_flags, (int)egm_i(M_CPM), egm_f(M_CPS), egm_f(M_USV), 0 };
  if (M_HAS_HV) {
    EGMeasureIter hv(0xFF, false, M_HV);
    if (hv.next()) v.hv = (int)hv.value().i;
    else v.flags &= ~SHOW_HV;   // no adc
  }
  char buf[80];
  size_t pos = labelled_line(buf, sizeof(buf), v);
  if (pos == 0) return;
  if (pos < sizeof(buf) - 1) buf[pos++] = '\n';
  send(buf, pos);
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
