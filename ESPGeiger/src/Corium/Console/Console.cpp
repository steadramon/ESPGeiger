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
#include "Corium/Module/Registry.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Net/Wifi.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/BootGuard.h"
#include "Corium/System/BootButton.h"
#include "Corium/ImprovSerial/ImprovSerial.h"
#include "Corium/Util/Words.h"

corium::Console console;

CORIUM_REGISTER_MODULE(console)

const corium::Cmd* corium::Console::_cmds[CORIUM_MAX_CMDS] = {};
uint8_t corium::Console::_count = 0;
uint8_t corium::Console::_dropped = 0;
bool corium::Console::_up = false;
corium::ConsoleLine corium::Console::_serial = {};
char* corium::Console::_last = nullptr;

static const char DELIM[] = " ";

static void reboot() { corium::DeviceInfo::safeRestart(); }
static void safe() {
  corium::BootGuard::scram();
  corium::DeviceInfo::safeRestart();
}

// The setup site reads this
static const char VER_LINE[] PROGMEM = THING_NAME " " RELEASE_VERSION " " BUILD_ENV;
static void ver() {
  corium::Log::console(VER_LINE);
  // show streaming turns console logging off, so print it straight out
  if (!corium::Log::serialOn()) Serial.println(FPSTR(VER_LINE));
}

// A frozen group already logged why when it was left out
static bool committed() {
  if (corium::Prefs::commit()) return true;
  char why[CORIUM_PREFS_FROZEN_MAX];
  if (!corium::Prefs::frozen(why, sizeof(why))) corium::Log::console(PSTR("Commit failed"));
  return false;
}

// Clears the dhcp/static settings too, same as /erase
static void reset_wifi() {
  corium::Prefs::reset("net");
  if (!corium::Wifi::joinMode() || !committed()) return;
  corium::Wifi::dropNetBackup();
  corium::Wifi::clearSavedCreds();
  reboot();
}

static void reset_net() {
  corium::Log::console(PSTR("Clearing net.* settings and restarting"));
  // back to dhcp with everything blank
  corium::Prefs::reset("net");
  if (!committed()) return;
  corium::Wifi::dropNetBackup();
  reboot();
}

static bool s_web = false;   // true while run() has a web command, not the serial line

#ifdef CORIUM_BOOT_BUTTON
static const char WIFI_BACK[] PROGMEM = "WiFi: Off after restart, undo: set wifi.mode 0 (serial) or button %ds at boot (reset)";
#else
static const char WIFI_BACK[] PROGMEM = "WiFi: Off after restart, undo: set wifi.mode 0";
#endif

// Without a boot button only serial can turn wifi back on, so the web can't turn it off
static bool off_refused(bool off, const char* mod) {
#ifndef CORIUM_BOOT_BUTTON
  if (off && s_web) {
    corium::Prefs::revert(mod);
    corium::Log::console(PSTR("Rejected: wifi.mode 1 needs serial"));
    return true;
  }
#endif
  return false;
}

// <module>.<key> of a known pref
struct PrefArg {
  const char* mod;
  const char* key;
  corium::Pref p;
};

static bool pref_arg(PrefArg& a, const char* usage) {
  char* arg = corium::Console::next();
  char* dot = arg ? strchr(arg, '.') : nullptr;
  if (!dot || dot == arg || !*(dot + 1)) {
    corium::Log::console(usage);
    return false;
  }
  *dot = '\0';
  a.mod = arg;
  a.key = dot + 1;
  if (corium::Prefs::findPref(a.mod, a.key, &a.p)) return true;
  corium::Log::console(PSTR("Unknown pref: %s.%s"), a.mod, a.key);
  return false;
}

static void cmd_get() {
  PrefArg a;
  if (!pref_arg(a, PSTR("Usage: get <module>.<key>"))) return;
  if (!corium::Prefs::loaded(a.mod, a.key)) {
    corium::Log::console(PSTR("%s.%s didn't load, reset %s"), a.mod, a.key, a.mod);
    return;
  }
  // never null, unset is ""
  const char* v = corium::Prefs::getString(a.mod, a.key);
  if ((a.p.flags & corium::P_SENSITIVE) && *v) v = PSTR("***");
  corium::Log::console(PSTR("%s.%s=%s"), a.mod, a.key, v);
}

static void cmd_set() {
  PrefArg a;
  if (!pref_arg(a, PSTR("Usage: set <module>.<key> <value>"))) return;
  const char* mod = a.mod;
  const char* key = a.key;
  char* value = corium::Console::rest();
  if (!value) value = (char*)"";
  while (*value == ' ') value++;
  size_t vlen = strlen(value);
  // only spaces to trim, a line never holds CR or LF
  while (vlen > 0 && value[vlen-1] == ' ') value[--vlen] = '\0';
  // Same as /wifisave, the backup lets the boot revert a static IP that fails
  if (!strcmp_P(mod, PSTR("net")) && !corium::Wifi::saveNetBackup()) {
    corium::Log::console(PSTR("Rejected: %s.%s no backup to go back to"), mod, key);
    return;
  }
  if (!corium::Prefs::put(mod, key, value)) {
    const char* why = corium::Prefs::refused();
    corium::Log::console(PSTR("Rejected: %s.%s %s"), mod, key, why ? why : W_BAD_VALUE);
    return;
  }
  bool off = corium::Wifi::offSet(mod, key);
  if (off_refused(off, mod)) return;
  if (!committed()) return;
  const char* ok = (a.p.flags & corium::P_SENSITIVE) ? PSTR("OK: %s.%s set") : PSTR("OK: %s.%s=%s");
  corium::Log::console(ok, mod, key, corium::Prefs::getString(mod, key));
  if (corium::Prefs::restartPending()) corium::Log::console(corium::H_RBA);
  if (off) corium::Log::console(WIFI_BACK, CORIUM_BOOT_BUTTON_RESET_S);
}

// Back to defaults, never reboots or wipes
static void cmd_reset() {
  char* arg = corium::Console::next();
  if (!arg) {
    corium::Log::console(PSTR("Usage: reset <module>.<key> | reset <module>"));
    return;
  }
  char* dot = strchr(arg, '.');
  if (dot) *dot = '\0';
  const char* key = dot ? dot + 1 : nullptr;
  bool ok = corium::Prefs::reset(arg, key);
  // Setting one key back to its default doesn't unfreeze it, only a group reset does
  bool stuck = ok && key && !corium::Prefs::loaded(arg, key);
  if (dot) *dot = '.';
  if (!ok) {
    corium::Log::console(PSTR("Rejected: reset %s"), arg);
    return;
  }
  if (!committed()) return;
  if (stuck) {
    corium::Log::console(PSTR("Rejected: %s didn't load, reset %.*s"), arg, (int)(dot - arg), arg);
    return;
  }
  corium::Log::console(PSTR("OK: %s reset"), arg);
  if (corium::Prefs::restartPending()) corium::Log::console(corium::H_RBA);
}

// This boot only, same as holding the button
static void offline() {
  if (corium::Wifi::offlineNow()) corium::Log::console(PSTR("WiFi: Offline this boot"));
  else corium::Log::console(PSTR("Offline: Boot only (setup portal or WiFi wait)"));
}

static const char C_REBOOT[] PROGMEM = "reboot";
static const char C_SAFE[] PROGMEM = "safe";
static const char C_RESETWIFI[] PROGMEM = "resetwifi";
static const char C_RESETNET[] PROGMEM = "resetnet";
static const char C_GET[] PROGMEM = "get";
static const char C_SET[] PROGMEM = "set";
static const char C_RESET[] PROGMEM = "reset";
static const char C_VER[] PROGMEM = "ver";
static const char C_OFFLINE[] PROGMEM = "offline";

static const corium::Cmd CMDS[] PROGMEM = {
  { C_REBOOT, reboot },
  { C_SAFE, safe },
  { C_RESETWIFI, reset_wifi },
  { C_RESETNET, reset_net },
  { C_GET, cmd_get },
  { C_SET, cmd_set },
  { C_RESET, cmd_reset },
  { C_VER, ver },
  { C_OFFLINE, offline },
};

bool corium::Console::add(const corium::Cmd* cmd) {
  if (_count >= CORIUM_MAX_CMDS) { _dropped++; return false; }
  _cmds[_count++] = cmd;
  return true;
}

char* corium::Console::next() {
  return strtok_r(NULL, DELIM, &_last);
}

char* corium::Console::rest() {
  return _last;
}

enum : uint8_t { RUN, HELD_BOOT, HELD_SAFE };

static bool run_row(const corium::Cmd* row, const char* word, uint8_t held) {
  corium::Cmd c;
  memcpy_P(&c, row, sizeof(c));
  if (strcmp_P(word, c.name) != 0) return false;
  if (held == HELD_SAFE) corium::Log::console(PSTR("%s: off in safe mode"), word);
  else if (held) corium::Log::console(PSTR("%s: after boot"), word);
  else c.fn();
  return true;
}

// false = no such command
bool corium::Console::exec(corium::ConsoleLine& l) {
  char* word = strtok_r(l.buf, DELIM, &_last);
  // App commands are held until their modules are up, and in safe mode
  const uint8_t own = sizeof(CMDS) / sizeof(CMDS[0]);
  const uint8_t app = !_up ? HELD_BOOT : corium::BootGuard::safeMode() ? HELD_SAFE : RUN;
  bool found = false;
  for (uint8_t i = 0; word && !found && i < own + _count; i++) {
    found = run_row(i < own ? &CMDS[i] : _cmds[i - own], word, i >= own ? app : RUN);
  }
  l.buf[0] = '\0';
  l.pos = 0;
  return found;
}

void corium::Console::begin() {
  _up = true;
  corium::Log::wake = [] { corium::Registry::post(&console); };
  if (_dropped) corium::Log::console(PSTR("Console: %u command(s) dropped (raise CORIUM_MAX_CMDS)"), _dropped);
#ifndef DISABLE_SERIALRX
  corium::Registry::every(this, LOOP_MS);   // only polled when serial rx is built in
#endif
}

void corium::Console::loop(unsigned long now) {
  corium::Log::flush();   // Lines logged from the 8266 tick get printed here
#ifndef DISABLE_SERIALRX
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (improvSerial.consumeByte((uint8_t)c)) continue;
    feed(_serial, Serial, c);
  }
  improvSerial.watch();
#endif
}

void corium::Console::pollBoot() {
#ifdef DISABLE_SERIALRX
  improvSerial.poll();   // rx pin belongs to the app, only improv is polled
#else
  loop(0);   // Improv takes its bytes first, the rest are typed commands
#endif
}

// A cut off line is never run, it could still save something
static void too_long() {
  corium::Log::console(PSTR("Command too long, max %u chars"), (unsigned)CONSOLE_LINE_MAX);
}

// For a set of a P_SENSITIVE pref returns where the value starts, else null
static char* secret_at(char* line) {
  const size_t n = sizeof(C_SET) - 1;
  char* s = line;
  while (*s == ' ') s++;
  if (strncmp_P(s, C_SET, n) != 0 || s[n] != ' ') return nullptr;
  s += n + 1;
  while (*s == ' ') s++;
  char* dot = strchr(s, '.');
  char* sp = dot ? strchr(dot, ' ') : nullptr;
  if (!sp) return nullptr;
  corium::Pref p;
  *dot = '\0';
  *sp = '\0';
  bool secret = corium::Prefs::findPref(s, dot + 1, &p) && (p.flags & corium::P_SENSITIVE);
  *dot = '.';
  *sp = ' ';
  return secret ? sp : nullptr;
}

// same as isprint() without the call
static bool typed(char c) { return c >= ' ' && c <= '~'; }

static bool line_end(char c) { return c == '\r' || c == '\n'; }

// Uses the serial line's buffer, so a half typed serial line is dropped
bool corium::Console::run(const char* text, size_t len) {
  if (!text) return false;
  corium::ConsoleLine& l = _serial;
  size_t n = 0;
  while (n < len && n <= CONSOLE_LINE_MAX && typed(text[n])) n++;
  if (n > CONSOLE_LINE_MAX) { too_long(); return false; }
  size_t end = n;
  while (end < len && line_end(text[end])) end++;
  if (end < len) {
    corium::Log::console(PSTR("Command refused: One line of printable ASCII"));
    return false;
  }
  memcpy(l.buf, text, n);
  l.buf[n] = '\0';
  l.pos = n;
  // Echo it through Log so /cs sees it
  char* secret = secret_at(l.buf);
  if (secret) corium::Log::console(PSTR("> %.*s ***"), (int)(secret - l.buf), l.buf);
  else corium::Log::console(PSTR("> %s"), l.buf);
  s_web = true;
  bool found = exec(l);
  s_web = false;
  return found;
}

void corium::Console::edit(corium::ConsoleLine& l, Print& io, char c) {
  if (c == 0x08 || c == 0x7f) {  // BS or DEL
    if (l.pos > 0) {
      l.buf[--l.pos] = '\0';
      io.write("\b \b", 3);
    }
  } else if (typed(c)) {
    if (l.pos >= CONSOLE_LINE_MAX) { l.overflow = true; return; }
    l.buf[l.pos++] = c;
    l.buf[l.pos] = '\0';
    io.write(c);
  }
}

void corium::Console::feed(corium::ConsoleLine& l, Print& io, char c) {
  if (c != '\r' && c != '\n') {
    edit(l, io, c);
    return;
  }
  // The LF of a CRLF gives an empty line, which runs nothing
  if (l.pos > 0) io.write("\r\n", 2);
  if (l.overflow) {
    too_long();
    l.overflow = false;
    l.buf[0] = '\0';   // so exec runs nothing and just clears it
  }
  exec(l);
}
