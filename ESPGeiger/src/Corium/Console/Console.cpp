/*
  Console.cpp - Serial and web console: line reader and command table

  Copyright (C) 2024 @steadramon

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
#include "Corium/Console/Console.h"
#include "App/Version.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Net/Wifi.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/BootGuard.h"
#include "Corium/ImprovSerial/ImprovSerial.h"

Console console;

EG_REGISTER_MODULE(console)

const EGCmd* Console::_cmds[EG_MAX_CMDS] = {};
uint8_t Console::_count = 0;
uint8_t Console::_dropped = 0;
ConsoleLine Console::_serial = {};
char* Console::_last = nullptr;

static const char DELIM[] = " ";

static void reboot() { DeviceInfo::safeRestart(); }
static void safe() {
  BootGuard::request_safe();
  DeviceInfo::safeRestart();
}

// setup site reads this
static const char VER_LINE[] PROGMEM = THING_NAME " " RELEASE_VERSION " " BUILD_ENV;
static void ver() {
  Log::console(VER_LINE);
  // show streaming turns console logging off
  if (!Log::serialOn()) Serial.println(FPSTR(VER_LINE));
}

static void reset_wifi() {
  Wifi::clearSavedCreds();
  reboot();
}

static bool committed() {
  if (EGPrefs::commit()) return true;
  Log::console(PSTR("Commit failed"));
  return false;
}

static void reset_net() {
  Log::console(PSTR("Clearing net.* prefs and rebooting"));
  // dhcp, all blank
  EGPrefs::reset("net");
  if (!committed()) return;
  Wifi::dropNetBackup();
  reboot();
}

// <module>.<key> of a known pref
struct PrefArg {
  const char* mod;
  const char* key;
  EGPref p;
};

static bool pref_arg(PrefArg& a, const char* usage) {
  char* arg = Console::next();
  char* dot = arg ? strchr(arg, '.') : nullptr;
  if (!dot || dot == arg || !*(dot + 1)) {
    Log::console(usage);
    return false;
  }
  *dot = '\0';
  a.mod = arg;
  a.key = dot + 1;
  if (EGPrefs::find_pref(a.mod, a.key, &a.p)) return true;
  Log::console(PSTR("Unknown pref: %s.%s"), a.mod, a.key);
  return false;
}

static void cmd_get() {
  PrefArg a;
  if (!pref_arg(a, PSTR("Usage: get <module>.<key>"))) return;
  // never null, unset is ""
  const char* v = EGPrefs::getString(a.mod, a.key);
  if ((a.p.flags & EGP_SENSITIVE) && *v) v = PSTR("***");
  Log::console(PSTR("%s.%s=%s"), a.mod, a.key, v);
}

static void cmd_set() {
  PrefArg a;
  if (!pref_arg(a, PSTR("Usage: set <module>.<key> <value>"))) return;
  const char* mod = a.mod;
  const char* key = a.key;
  // rest of line, keeps spaces
  char* value = Console::rest();
  if (!value) value = (char*)"";
  while (*value == ' ') value++;
  size_t vlen = strlen(value);
  // a line never holds CR or LF
  while (vlen > 0 && value[vlen-1] == ' ') value[--vlen] = '\0';
  if (!EGPrefs::put(mod, key, value)) {
    Log::console(PSTR("Rejected: %s.%s validation failed"), mod, key);
    return;
  }
  if (!committed()) return;
  const char* ok = (a.p.flags & EGP_SENSITIVE) ? PSTR("OK: %s.%s set") : PSTR("OK: %s.%s=%s");
  Log::console(ok, mod, key, EGPrefs::getString(mod, key));
  if (EGPrefs::restart_pending()) Log::console(EG_H_RBA);
}

// back to defaults, never reboots or wipes
static void cmd_reset() {
  char* arg = Console::next();
  if (!arg) {
    Log::console(PSTR("Usage: reset <module>.<key> | reset <module>"));
    return;
  }
  char* dot = strchr(arg, '.');
  if (dot) *dot = '\0';
  bool ok = EGPrefs::reset(arg, dot ? dot + 1 : nullptr);
  // whole word again for the reply
  if (dot) *dot = '.';
  if (!ok) {
    Log::console(PSTR("Rejected: reset %s"), arg);
    return;
  }
  if (!committed()) return;
  Log::console(PSTR("OK: %s reset"), arg);
  if (EGPrefs::restart_pending()) Log::console(EG_H_RBA);
}

static const char C_REBOOT[] PROGMEM = "reboot";
static const char C_SAFE[] PROGMEM = "safe";
static const char C_RESETWIFI[] PROGMEM = "resetwifi";
static const char C_RESETNET[] PROGMEM = "resetnet";
static const char C_GET[] PROGMEM = "get";
static const char C_SET[] PROGMEM = "set";
static const char C_RESET[] PROGMEM = "reset";
static const char C_VER[] PROGMEM = "ver";

// in flash, no RAM copy
static const EGCmd CMDS[] PROGMEM = {
  { C_REBOOT, reboot },
  { C_SAFE, safe },
  { C_RESETWIFI, reset_wifi },
  { C_RESETNET, reset_net },
  { C_GET, cmd_get },
  { C_SET, cmd_set },
  { C_RESET, cmd_reset },
  { C_VER, ver },
};

bool Console::add(const EGCmd* cmd) {
  if (_count >= EG_MAX_CMDS) { _dropped++; return false; }
  _cmds[_count++] = cmd;
  return true;
}

char* Console::next() {
  return strtok_r(NULL, DELIM, &_last);
}

char* Console::rest() {
  return _last;
}

static bool run_row(const EGCmd* row, const char* word) {
  EGCmd c;
  memcpy_P(&c, row, sizeof(c));
  if (strcmp_P(word, c.name) != 0) return false;
  c.fn();
  return true;
}

void Console::exec(ConsoleLine& l) {
  char* word = strtok_r(l.buf, DELIM, &_last);
  // own rows, then the app's
  const uint8_t own = sizeof(CMDS) / sizeof(CMDS[0]);
  for (uint8_t i = 0; word && i < own + _count; i++) {
    if (run_row(i < own ? &CMDS[i] : _cmds[i - own], word)) break;
  }
  l.buf[0] = '\0';
  l.pos = 0;
}

void Console::begin() {
  if (_dropped) Log::console(PSTR("Console: %u command(s) dropped (raise EG_MAX_CMDS)"), _dropped);
#ifndef DISABLE_SERIALRX
  EGModuleRegistry::every(this, LOOP_MS);   // else nothing to read
#endif
}

void Console::loop(unsigned long now) {
#ifndef DISABLE_SERIALRX
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (improvSerial.consume_byte((uint8_t)c)) continue;
    feed(_serial, Serial, c);
  }
#endif
}

// a cut line could still save
static void too_long() {
  Log::console(PSTR("Command too long, max %u chars"), (unsigned)CONSOLE_LINE_MAX);
}

// today: shares the serial line, a half typed one is dropped
void Console::run(const char* text) {
  if (!text) return;
  ConsoleLine& l = _serial;
  size_t n = 0;
  while (text[n] && n <= CONSOLE_LINE_MAX && text[n] != '\r' && text[n] != '\n') n++;
  if (n > CONSOLE_LINE_MAX) { too_long(); return; }
  memcpy(l.buf, text, n);
  l.buf[n] = '\0';
  l.pos = n;
  // via Log so /cs sees it
  Log::console(PSTR("> %s"), l.buf);
  exec(l);
}

void Console::edit(ConsoleLine& l, Print& io, char c) {
  if (c == 0x08 || c == 0x7f) {  // BS or DEL
    if (l.pos > 0) {
      l.buf[--l.pos] = '\0';
      io.write("\b \b", 3);
    }
  } else if (c >= ' ' && c <= '~') {  // isprint, no call
    if (l.pos >= CONSOLE_LINE_MAX) { l.overflow = true; return; }
    l.buf[l.pos++] = c;
    l.buf[l.pos] = '\0';
    io.write(c);
  }
}

void Console::feed(ConsoleLine& l, Print& io, char c) {
  if (c != '\r' && c != '\n') {
    edit(l, io, c);
    return;
  }
  // CRLF gives an empty line
  if (l.pos > 0) io.write("\r\n", 2);
  if (l.overflow) {
    too_long();
    l.overflow = false;
    l.buf[0] = '\0';   // exec runs nothing, clears
  }
  exec(l);
}
