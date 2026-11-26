/*
  Console.h - Serial and web console: line reader and command table

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
#ifndef CORIUM_CONSOLE_H
#define CORIUM_CONSOLE_H
#include <Arduino.h>
#include <Print.h>
#include "Corium/Module/Module.h"

#ifndef CONSOLE_LINE_MAX
#define CONSOLE_LINE_MAX 255
#endif
static_assert(CONSOLE_LINE_MAX < 256, "line pos is 8 bit");

// Max app commands, Corium's own sit in a fixed table
#ifndef CORIUM_MAX_CMDS
#define CORIUM_MAX_CMDS 8
#endif

namespace corium {

// Lives in PROGMEM, and so does the name
struct Cmd {
  const char* name;
  void (*fn)();
};

// Line being typed, one per input
struct ConsoleLine {
  char buf[CONSOLE_LINE_MAX + 1];
  uint8_t pos;
  bool overflow;   // too long, dropped when the line ends
};

class Console : public Module {
public:
    const char* name() override { return PSTR("scmd"); }
    bool coreModule() override { return true; }
    static constexpr uint16_t LOOP_MS = 10;   // ms between serial reads
    void begin() override;
    void loop(unsigned long now) override;
    void pollBoot();   // From the setup portal and wifi wait, before the registry runs

    static bool add(const Cmd* cmd);   // static init, false = full
    static bool run(const char* text, size_t len);   // one line from the web, false = refused or unknown
    // For reading a command's words while it runs
    static char* next();   // nullptr = none left
    static char* rest();   // Rest of the line as typed
private:
    static void feed(ConsoleLine& l, Print& io, char c);
    static void edit(ConsoleLine& l, Print& io, char c);
    static bool exec(ConsoleLine& l);
    static const Cmd* _cmds[CORIUM_MAX_CMDS];
    static uint8_t _count;
    static uint8_t _dropped;
    static bool _up;   // set by begin(), app commands can run
    static ConsoleLine _serial;
    static char* _last;   // where strtok_r is up to
};
}  // namespace corium

extern corium::Console console;

// File scope, adds a console word at static init
#define CORIUM_REGISTER_CMD(word, fn) \
  static const char _cmdn_##word[] PROGMEM = #word; \
  static const corium::Cmd _cmd_##word PROGMEM = {_cmdn_##word, fn}; \
  static bool _cmdr_##word = corium::Console::add(&_cmd_##word);

#endif
