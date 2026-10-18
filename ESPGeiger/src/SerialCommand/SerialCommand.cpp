/*
  SerialCommand.cpp - Serial Command class

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
#include "SerialCommand.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Module/ExternalPause.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/Logger/Logger.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/BootGuard.h"
#include "Corium/System/Fs.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/ImprovSerial/ImprovSerial.h"

void SerialCommand::reboot() { DeviceInfo::safeRestart(); }
static void safe() {
  BootGuard::request_safe();
  DeviceInfo::safeRestart();
}

// setup site reads this
static const char VER_LINE[] PROGMEM = "ESPGeiger " RELEASE_VERSION " " BUILD_ENV;
static void ver() {
  Log::console(VER_LINE);
  // show streaming turns console logging off
  if (!Log::serialOn()) Serial.println(FPSTR(VER_LINE));
}
SerialCommand serialcmd;

EG_REGISTER_MODULE(serialcmd)

/**
 * Constructor makes sure some things are set.
 */
SerialCommand::SerialCommand()
  : defaultHandler(NULL),
    term('\n'),           // default terminator for commands, newline character
    last(NULL)
{
  delim[0] = ' ';
  delim[1] = '\0';
  clearBuffer();
}

#ifdef EG_HV_STALL_TEST
static void t1stop() { timer1_disable(); }
#endif

struct Cmd {
  const char* name;
  void (*fn)();
};

static const char C_REBOOT[] PROGMEM = "reboot";
static const char C_SAFE[] PROGMEM = "safe";
static const char C_RESETWIFI[] PROGMEM = "resetwifi";
static const char C_RESETNET[] PROGMEM = "resetnet";
static const char C_GET[] PROGMEM = "get";
static const char C_SET[] PROGMEM = "set";
static const char C_PAUSE[] PROGMEM = "pause";
static const char C_VER[] PROGMEM = "ver";
#ifdef SERIALOUT
static const char C_CPM[] PROGMEM = "cpm";
static const char C_USV[] PROGMEM = "usv";
static const char C_SHOW[] PROGMEM = "show";
#endif
#if GEIGER_IS_TEST(GEIGER_TYPE)
static const char C_TARGET[] PROGMEM = "target";
#endif
#ifdef ESPG_HV_ADC
static const char C_HV[] PROGMEM = "hv";
#endif
#ifdef EG_HV_STALL_TEST
static const char C_T1STOP[] PROGMEM = "t1stop";
#endif

// in flash, no RAM copy
static const Cmd CMDS[] PROGMEM = {
  { C_REBOOT, SerialCommand::reboot },
  { C_SAFE, safe },
  { C_RESETWIFI, SerialCommand::reset_wifi },
  { C_RESETNET, SerialCommand::reset_net },
  { C_GET, SerialCommand::cmd_get },
  { C_SET, SerialCommand::cmd_set },
  { C_PAUSE, SerialCommand::cmd_pause },
  { C_VER, ver },
#ifdef SERIALOUT
  { C_CPM, SerialCommand::get_cpm },
  { C_USV, SerialCommand::get_usv },
  { C_SHOW, SerialCommand::set_show },
#endif
#if GEIGER_IS_TEST(GEIGER_TYPE)
  { C_TARGET, SerialCommand::set_cpm },
#endif
#ifdef ESPG_HV_ADC
  { C_HV, SerialCommand::get_hv },
#endif
#ifdef EG_HV_STALL_TEST
  { C_T1STOP, t1stop },
#endif
};
#ifdef SERIALOUT
static bool show_toggle(const char* arg) {
  if      (strcmp_P(arg, PSTR("cpm")) == 0) serialout.toggle_cpm();
  else if (strcmp_P(arg, PSTR("usv")) == 0) serialout.toggle_usv();
  else if (strcmp_P(arg, PSTR("cps")) == 0) serialout.toggle_cps();
  else if (strcmp_P(arg, PSTR("hv"))  == 0) {
#ifdef ESPG_HV_ADC
    serialout.toggle_hv();
#endif
  }
  else return false;
  return true;
}

void SerialCommand::set_show() {
  char* arg = strtok_r(NULL, serialcmd.delim, &serialcmd.last);
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

void SerialCommand::get_cpm() {
  serialout.print_cpm();
}

void SerialCommand::get_usv() {
  serialout.print_usv();
}

#endif
void SerialCommand::reset_wifi() {
  Wifi::clearSavedCreds();
  reboot();
}

// pause [s], 0 resumes
void SerialCommand::cmd_pause() {
  char* arg = strtok_r(NULL, serialcmd.delim, &serialcmd.last);
  if (arg) {
    if (!ExternalPause::start_s(arg)) Log::console(PSTR("pause [seconds], 0 resumes"));
    return;
  }
  uint32_t rem = ExternalPause::remaining_ms();
  if (rem) Log::console(PSTR("External posts: paused %u s remaining"), (unsigned)(rem / 1000U));
  else     Log::console(PSTR("External posts: active"));
}

static const char UNKNOWN_PREF[] PROGMEM = "Unknown pref: %s.%s";

static bool split_modkey(char* arg, const char** mod, const char** key) {
  if (!arg) return false;
  char* dot = strchr(arg, '.');
  if (!dot || dot == arg || !*(dot + 1)) return false;
  *dot = '\0';
  *mod = arg;
  *key = dot + 1;
  return true;
}

void SerialCommand::cmd_get() {
  char* arg = strtok_r(NULL, serialcmd.delim, &serialcmd.last);
  const char *mod, *key;
  if (!split_modkey(arg, &mod, &key)) {
    Log::console(PSTR("Usage: get <module>.<key>"));
    return;
  }
  EGPref p;
  if (!EGPrefs::find_pref(mod, key, &p)) {
    Log::console(UNKNOWN_PREF, mod, key);
    return;
  }
  const char* v = EGPrefs::getString(mod, key);
  if (p.flags & EGP_SENSITIVE) v = (v && *v) ? PSTR("***") : PSTR("");
  Log::console(PSTR("%s.%s=%s"), mod, key, v ? v : "");
}

void SerialCommand::cmd_set() {
  char* arg = strtok_r(NULL, serialcmd.delim, &serialcmd.last);
  const char *mod, *key;
  if (!split_modkey(arg, &mod, &key)) {
    Log::console(PSTR("Usage: set <module>.<key> <value>"));
    return;
  }
  EGPref p;
  if (!EGPrefs::find_pref(mod, key, &p)) {
    Log::console(UNKNOWN_PREF, mod, key);
    return;
  }
  // rest of line, keeps spaces
  char* value = serialcmd.last;
  if (!value) value = (char*)"";
  while (*value == ' ') value++;
  size_t vlen = strlen(value);
  while (vlen > 0 && (value[vlen-1] == ' ' || value[vlen-1] == '\r' || value[vlen-1] == '\n')) {
    value[--vlen] = '\0';
  }
  if (!EGPrefs::put(mod, key, value)) {
    Log::console(PSTR("Rejected: %s.%s validation failed"), mod, key);
    return;
  }
  if (!EGPrefs::commit()) {
    Log::console(PSTR("Commit failed"));
    return;
  }
  if (p.flags & EGP_SENSITIVE) {
    Log::console(PSTR("OK: %s.%s set"), mod, key);
  } else {
    Log::console(PSTR("OK: %s.%s=%s"), mod, key, EGPrefs::getString(mod, key));
  }
  if (EGPrefs::restart_pending()) Log::console(PSTR("Reboot to apply"));
}

void SerialCommand::reset_net() {
  Log::console(PSTR("Clearing net.* prefs and rebooting"));
  EGPrefs::put("net", EG_K_STATIC_IP, "0");
  EGPrefs::put("net", EG_K_IP,     "");
  EGPrefs::put("net", EG_K_GW,     "");
  EGPrefs::put("net", EG_K_SN,     "");
  EGPrefs::put("net", EG_K_DNS,    "");
  EGPrefs::commit();
  Wifi::dropNetBackup();
  reboot();
}

#if GEIGER_IS_TEST(GEIGER_TYPE)
void SerialCommand::set_cpm() {
  char* arg = strtok_r(NULL, serialcmd.delim, &serialcmd.last);
  if (arg == NULL) return;
  int32_t aNumber;
  if (parse_i32(arg, &aNumber) && aNumber > 0) gcounter.set_target_cpm(aNumber);
}
#endif

#ifdef ESPG_HV_ADC
void SerialCommand::get_hv() {
  Log::console(PSTR("HV: %d"), (int)hv.hvReading.get());
}
#endif

void SerialCommand::loop(unsigned long now) {
#ifndef DISABLE_SERIALRX
  readSerial();
#endif
}

// a cut line could still save
static void too_long() {
  Log::console(PSTR("Command too long, max %u chars"), (unsigned)SERIALCOMMAND_BUFFER);
}

void SerialCommand::dispatch(const char* line) {
  if (!line) return;
  size_t n = strnlen(line, SERIALCOMMAND_BUFFER + 1);
  if (n > SERIALCOMMAND_BUFFER) { too_long(); return; }
  memcpy(buffer, line, n);
  buffer[n] = '\0';
  bufPos = n;
  // via Log so /cs sees it
  Log::console(PSTR("> %s"), buffer);
  run_buffer();
}

void SerialCommand::run_buffer() {
  char* command = strtok_r(buffer, delim, &last);
  if (command) {
    bool matched = false;
    for (const Cmd& row : CMDS) {
      Cmd c;
      memcpy_P(&c, &row, sizeof(c));
      if (strncmp_P(command, c.name, SERIALCOMMAND_MAXCOMMANDLENGTH) == 0) {
        c.fn();
        matched = true;
        break;
      }
    }
    if (!matched && defaultHandler) (*defaultHandler)(command);
  }
  clearBuffer();
}

void SerialCommand::edit_line(char inChar) {
  if (inChar == 0x08 || inChar == 0x7f) {  // BS or DEL
    if (bufPos > 0) {
      buffer[--bufPos] = '\0';
      Serial.write("\b \b", 3);
    }
  } else if (isprint(inChar)) {
    if (bufPos >= SERIALCOMMAND_BUFFER) { overflow = true; return; }
    buffer[bufPos++] = inChar;
    buffer[bufPos] = '\0';
    Serial.write(inChar);
  }
}

void SerialCommand::readSerial() {
  while (Serial.available() > 0) {
    char inChar = Serial.read();
    if (improvSerial.consume_byte((uint8_t)inChar)) continue;

    if (inChar == term || inChar == '\r' || inChar == '\n') {
      // CRLF gives an empty line
      if (bufPos > 0) Serial.println();
      if (overflow) {
        too_long();
        clearBuffer();
        overflow = false;
      } else {
        run_buffer();
      }
    } else {
      edit_line(inChar);
    }
  }
}

void SerialCommand::clearBuffer() {
  buffer[0] = '\0';
  bufPos = 0;
}

char *SerialCommand::next() {
  return strtok_r(NULL, delim, &last);
}
