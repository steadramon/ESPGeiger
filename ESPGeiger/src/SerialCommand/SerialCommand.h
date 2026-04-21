/*
  SerialCommand.h - Serial Command class
  
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
#ifndef _SERIALCOMMAND_h_
#define _SERIALCOMMAND_h_
#include <Arduino.h>
#include "../Counter/Counter.h"
#include "../ConfigManager/ConfigManager.h"
#include "../Module/EGModule.h"

#ifdef ESPGEIGER_HW
#include "../ESPGHW/ESPGHW.h"
extern ESPGeigerHW hardware;
#endif

#define SERIALCOMMAND_MAXCOMMANDLENGTH 12
#define SERIALCOMMAND_BUFFER 32
#define MAX_COMMANDS 32
//#define SERIALCOMMAND_DEBUG true

extern Counter gcounter;
#ifdef SERIALOUT
#include "../SerialOut/SerialOut.h"
extern SerialOut serialout;
#endif

typedef void (* cbFunction)(char*);

class SerialCommand : public EGModule {
public:
    SerialCommand();
    const char* name() override { return "scmd"; }
    uint16_t warmup_seconds() override { return 0; }
    void begin() override { setup(); }
    void loop(unsigned long now) override;
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return 10; }
    void setup();
    void readSerial();
    void clearBuffer();
    char *next();
    void addCommand(const char *command, void(*function)());
    static void reboot() { ESP.restart(); };
    static void reset_wifi();
    static void set_ratio();
#ifdef SERIALOUT
    static void get_cpm();
    static void get_usv();
    static void set_show();
#endif
#if GEIGER_IS_TEST(GEIGER_TYPE)
    static void set_cpm();
#endif
#ifdef ESPGEIGER_HW
    static void get_hv();
    static void set_freq();
    static void set_duty();
    static void set_vdratio();
    static void set_vdoffset();
#endif
private:
    struct SerialCommandCallback {
      char command[SERIALCOMMAND_MAXCOMMANDLENGTH + 1];
      void (*function)();
    };
    SerialCommandCallback *commandList;
    byte commandCount;
    void (*defaultHandler)(const char *);
    
    char delim[2]; // null-terminated list of character to be used as delimeters for tokenizing (default " ")
    char term;     // Character that signals end of command (default '\n')
    char buffer[SERIALCOMMAND_BUFFER + 1]; // Buffer of stored characters while waiting for terminator character
    byte bufPos;                        // Current position in the buffer
    char *last;                         // State variable used by strtok_r during processing
};
#endif