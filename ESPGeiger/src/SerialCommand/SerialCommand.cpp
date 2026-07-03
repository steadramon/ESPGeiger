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
#include "../Module/EGModuleRegistry.h"
#include "../Prefs/EGPrefs.h"
#include "../Logger/Logger.h"
#include "../Util/DeviceInfo.h"
#include "../ImprovSerial/ImprovSerial.h"
#include <LittleFS.h>

void SerialCommand::reboot() { DeviceInfo::safeRestart(); }
extern SerialCommand serialcmd;

EG_REGISTER_MODULE(serialcmd)

/**
 * Constructor makes sure some things are set.
 */
SerialCommand::SerialCommand()
  : commandList(NULL),
    commandCount(0),
    defaultHandler(NULL),
    term('\n'),           // default terminator for commands, newline character
    last(NULL)
{
  strcpy(delim, " "); // strtok_r needs a null-terminated string
  clearBuffer();
}

void SerialCommand::setup() {
  addCommand(PSTR("reboot"), reboot);
  addCommand(PSTR("resetwifi"), reset_wifi);
  addCommand(PSTR("resetnet"), reset_net);
  addCommand(PSTR("get"), cmd_get);
  addCommand(PSTR("set"), cmd_set);
  addCommand(PSTR("pause"), cmd_pause);
#ifdef SERIALOUT
  addCommand(PSTR("cpm"), get_cpm);
  addCommand(PSTR("usv"), get_usv);
  addCommand(PSTR("show"), set_show);
#endif
#if GEIGER_IS_TEST(GEIGER_TYPE)
  addCommand(PSTR("target"), set_cpm);
#endif
#ifdef ESPG_HV_ADC
  addCommand(PSTR("hv"), get_hv);
#endif
}

void SerialCommand::addCommand(const char *command, void (*function)()) {
  #ifdef SERIALCOMMAND_DEBUG
    Serial.print(F("Adding command ("));
    Serial.print(commandCount);
    Serial.print(F("): "));
    Serial.println(command);
  #endif

  SerialCommandCallback *tmp = (SerialCommandCallback *) realloc(commandList, (commandCount + 1) * sizeof(SerialCommandCallback));
  if (tmp == NULL) {
    return;
  }
  commandList = tmp;
  // command is a PSTR; the slot is uninitialised realloc memory and
  // strncpy_P does not terminate when the source fills the field.
  strncpy_P(commandList[commandCount].command, command, SERIALCOMMAND_MAXCOMMANDLENGTH);
  commandList[commandCount].command[SERIALCOMMAND_MAXCOMMANDLENGTH] = '\0';
  commandList[commandCount].function = function;
  commandCount++;
}
#ifdef SERIALOUT
void SerialCommand::set_show() {
  char *arg;
  int aNumber;
  arg = strtok_r(NULL, serialcmd.delim, &serialcmd.last);
  if (arg != NULL) {
    if (strcmp(arg, "cpm") == 0) { serialout.toggle_cpm(); return; }
    if (strcmp(arg, "usv") == 0) { serialout.toggle_usv(); return; }
    if (strcmp(arg, "cps") == 0) { serialout.toggle_cps(); return; }
    if (strcmp(arg, "hv")  == 0) {
#ifdef ESPG_HV_ADC
      serialout.toggle_hv();
#endif
      return;
    }
    if (strcmp(arg, "off") == 0) { serialout.set_show(0); return; }
    if (isDigit(arg[0])) {
      aNumber = atoi(arg);
      serialout.set_show(aNumber);
      return;
    }
    Log::console(PSTR("show: unknown arg '%s'"), arg);
  } else {
    // Bare `show` toggles: off if currently running, otherwise default-on
    // (1-second interval, CPM if no fields previously selected).
    serialout.set_show(serialout.interval() > 0 ? 0 : 1);
  }
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
  // Also drop /net_backup so a stuck static-IP probe doesn't fire after
  // the user reconfigures via the captive setup portal.
  if (LittleFS.begin()) {
    LittleFS.remove("/net_backup");
    LittleFS.end();
  }
  reboot();
}

// `pause [seconds]` - 0 resumes, no arg prints remaining. Cap 24 h.
void SerialCommand::cmd_pause() {
  char* arg = strtok_r(NULL, serialcmd.delim, &serialcmd.last);
  if (arg) {
    int n = atoi(arg);
    if (n < 0) n = 0;
    if (n > 86400) n = 86400;
    Counter::pause_external((uint32_t)n * 1000U);
    return;
  }
  uint32_t rem = Counter::pause_remaining_ms();
  if (rem) Log::console(PSTR("External posts: paused %u s remaining"), (unsigned)(rem / 1000U));
  else     Log::console(PSTR("External posts: active"));
}

// Split "module.key" into separate strings. Mutates `arg` in place.
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
  const EGPref* p = EGPrefs::find_pref(mod, key);
  if (!p) {
    Log::console(PSTR("Unknown pref: %s.%s"), mod, key);
    return;
  }
  const char* v = EGPrefs::getString(mod, key);
  if (p->flags & EGP_SENSITIVE) {
    Log::console(PSTR("%s.%s=%s"), mod, key, (v && *v) ? "***" : "");
  } else {
    Log::console(PSTR("%s.%s=%s"), mod, key, v ? v : "");
  }
}

void SerialCommand::cmd_set() {
  char* arg = strtok_r(NULL, serialcmd.delim, &serialcmd.last);
  const char *mod, *key;
  if (!split_modkey(arg, &mod, &key)) {
    Log::console(PSTR("Usage: set <module>.<key> <value>"));
    return;
  }
  const EGPref* p = EGPrefs::find_pref(mod, key);
  if (!p) {
    Log::console(PSTR("Unknown pref: %s.%s"), mod, key);
    return;
  }
  // Use the rest of the buffer verbatim - preserves embedded spaces in
  // string values (passwords, friendly names). last points just past the
  // null we wrote at the end of the previous token.
  char* value = serialcmd.last;
  if (!value) value = (char*)"";
  // Skip leading spaces from the delimiter.
  while (*value == ' ') value++;
  // Trim trailing whitespace (the readSerial loop strips the terminator
  // but a stray CR can sneak through if both CR and LF arrived together).
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
  if (p->flags & EGP_SENSITIVE) {
    Log::console(PSTR("OK: %s.%s set"), mod, key);
  } else {
    Log::console(PSTR("OK: %s.%s=%s"), mod, key, EGPrefs::getString(mod, key));
  }
}

// Recovery hatch - wipes net.* prefs only (static_ip + ip/gw/sn/dns).
// Use when a bad static IP makes the web UI unreachable but WiFi creds
// are still good. Less destructive than `resetwifi` (preserves the AP
// association) and FULL_RESET (preserves all other prefs).
void SerialCommand::reset_net() {
  Log::console(PSTR("Clearing net.* prefs and rebooting"));
  EGPrefs::put("net", "static_ip", "0");
  EGPrefs::put("net", "ip",        "");
  EGPrefs::put("net", "gw",        "");
  EGPrefs::put("net", "sn",        "");
  EGPrefs::put("net", "dns",       "");
  EGPrefs::commit();
  if (LittleFS.begin()) {
    LittleFS.remove("/net_backup");
    LittleFS.end();
  }
  reboot();
}

#if GEIGER_IS_TEST(GEIGER_TYPE)
void SerialCommand::set_cpm() {
  char* arg = strtok_r(NULL, serialcmd.delim, &serialcmd.last);
  if (arg == NULL) return;
  int aNumber = atoi(arg);
  if (aNumber > 0) gcounter.set_target_cpm(aNumber);
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

void SerialCommand::dispatch(const char* line) {
  if (!line) return;
  // Copy into the shared buffer so strtok_r can mutate it; truncate if
  // the caller's line is too long.
  size_t n = 0;
  while (n < SERIALCOMMAND_BUFFER && line[n]) {
    buffer[n] = line[n];
    n++;
  }
  buffer[n] = '\0';
  bufPos = n;
  // Echo via Log so the web /cs stream picks it up too (plain Serial
  // output bypasses the rotating log buffer).
  Log::console(PSTR("> %s"), buffer);
  char* command = strtok_r(buffer, delim, &last);
  if (command) {
    bool matched = false;
    for (int i = 0; i < commandCount; i++) {
      if (strncmp(command, commandList[i].command, SERIALCOMMAND_MAXCOMMANDLENGTH) == 0) {
        (*commandList[i].function)();
        matched = true;
        break;
      }
    }
    if (!matched && defaultHandler) (*defaultHandler)(command);
  }
  clearBuffer();
}

void SerialCommand::readSerial() {
  while (Serial.available() > 0) {
    char inChar = Serial.read();   // Read single available character, there may be more waiting
    // Improv WiFi provisioning frames arrive over the same serial port.
    // If the parser is mid-frame it consumes the byte; otherwise we keep it.
    if (improvSerial.consume_byte((uint8_t)inChar)) continue;

    // Accept either CR or LF as line terminator - saves users having to
    // configure their serial monitor's line-ending mode.
    if (inChar == term || inChar == '\r' || inChar == '\n') {
      // Echo a newline so the next log line starts on its own row;
      // suppress empty lines (common when CRLF arrives - first char
      // dispatched the command, second char hits an empty buffer).
      if (bufPos > 0) Serial.println();
      #ifdef SERIALCOMMAND_DEBUG
        Serial.print(F("Received: "));
        Serial.println(buffer);
      #endif
      char *command = strtok_r(buffer, delim, &last);   // Search for command at start of buffer
      if (command != NULL) {
        boolean matched = false;
        for (int i = 0; i < commandCount; i++) {
          #ifdef SERIALCOMMAND_DEBUG
            Serial.print(F("Comparing ["));
            Serial.print(command);
            Serial.print(F("] to ["));
            Serial.print(commandList[i].command);
            Serial.println(F("]"));
          #endif

          // Compare the found command against the list of known commands for a match
          if (strncmp(command, commandList[i].command, SERIALCOMMAND_MAXCOMMANDLENGTH) == 0) {
            #ifdef SERIALCOMMAND_DEBUG
              Serial.print(F("Matched Command: "));
              Serial.println(command);
            #endif

            // Execute the stored handler function for the command
            (*commandList[i].function)();
            matched = true;
            break;
          }
        }
        if (!matched && (defaultHandler != NULL)) {
          (*defaultHandler)(command);
        }
      }
      clearBuffer();
    }
    else if (inChar == 0x08 || inChar == 0x7f) {  // BS or DEL
      if (bufPos > 0) {
        buffer[--bufPos] = '\0';
        Serial.write("\b \b", 3);   // back, space, back - visually wipes
      }
    }
    else if (isprint(inChar)) {     // Only printable characters into the buffer
      if (bufPos < SERIALCOMMAND_BUFFER) {
        buffer[bufPos++] = inChar;  // Put character into buffer
        buffer[bufPos] = '\0';      // Null terminate
        Serial.write(inChar);       // Echo so the user sees what they typed
      } else {
        #ifdef SERIALCOMMAND_DEBUG
          Serial.println(F("Line buffer is full - increase SERIALCOMMAND_BUFFER"));
        #endif
      }
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