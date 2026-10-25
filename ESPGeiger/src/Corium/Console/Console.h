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
#ifndef CONSOLE_H
#define CONSOLE_H
#include <Arduino.h>
#include <Print.h>
#include "Corium/Module/EGModule.h"

#ifndef CONSOLE_LINE_MAX
#define CONSOLE_LINE_MAX 64
#endif
static_assert(CONSOLE_LINE_MAX < 256, "line pos is 8 bit");

// app commands, Corium's own sit in a fixed table
#ifndef EG_MAX_CMDS
#define EG_MAX_CMDS 8
#endif

// PROGMEM, the name too
struct EGCmd {
  const char* name;
  void (*fn)();
};

// typed bytes, one per input
struct ConsoleLine {
  char buf[CONSOLE_LINE_MAX + 1];
  uint8_t pos;
  bool overflow;   // too long, dropped at the end
};

class Console : public EGModule {
public:
    const char* name() override { return PSTR("scmd"); }
    bool core_module() override { return true; }
    static constexpr uint16_t LOOP_MS = 10;
    void begin() override;
    void loop(unsigned long now) override;

    static bool add(const EGCmd* cmd);   // static init, false = full
    static void run(const char* text);   // web, first line only
    // a command's words, as it runs
    static char* next();   // nullptr = none left
    static char* rest();   // rest of the line as typed
private:
    static void feed(ConsoleLine& l, Print& io, char c);
    static void edit(ConsoleLine& l, Print& io, char c);
    static void exec(ConsoleLine& l);
    static const EGCmd* _cmds[EG_MAX_CMDS];
    static uint8_t _count;
    static uint8_t _dropped;
    static ConsoleLine _serial;
    static char* _last;   // strtok_r's place
};

extern Console console;

#define EG_REGISTER_CMD(word, fn) \
  static const char _cmdn_##word[] PROGMEM = #word; \
  static const EGCmd _cmd_##word PROGMEM = {_cmdn_##word, fn}; \
  static bool _cmdr_##word = Console::add(&_cmd_##word);

#endif
