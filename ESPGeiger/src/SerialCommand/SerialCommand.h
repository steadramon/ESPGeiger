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
#ifndef SERIALCOMMAND_h_
#define SERIALCOMMAND_h_
#include <Arduino.h>
#include "Counter/Counter.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Module/EGModule.h"

#ifdef ESPG_HV
#include "HV/HV.h"
#endif

#define SERIALCOMMAND_MAXCOMMANDLENGTH 12
#define SERIALCOMMAND_BUFFER 64

#ifdef SERIALOUT
#include "SerialOut/SerialOut.h"
#endif

typedef void (* cbFunction)(char*);

class SerialCommand : public EGModule {
public:
    SerialCommand();
    const char* name() override { return PSTR("scmd"); }
    uint16_t warmup_seconds() override { return 0; }
    bool core_module() override { return true; }
    void loop(unsigned long now) override;
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return 10; }
    void readSerial();
    void clearBuffer();
    char *next();
    void dispatch(const char* line);
    static void reboot();
    static void reset_wifi();
    static void reset_net();
    static void cmd_get();
    static void cmd_set();
    static void cmd_pause();
#ifdef SERIALOUT
    static void get_cpm();
    static void get_usv();
    static void set_show();
#endif
#if GEIGER_IS_TEST(GEIGER_TYPE)
    static void set_cpm();
#endif
#ifdef ESPG_HV_ADC
    static void get_hv();
#endif
private:
    void run_buffer();
    void edit_line(char inChar);
    void (*defaultHandler)(const char *);

    char delim[2]; // null-terminated list of character to be used as delimeters for tokenizing (default " ")
    char term;     // Character that signals end of command (default '\n')
    char buffer[SERIALCOMMAND_BUFFER + 1]; // Buffer of stored characters while waiting for terminator character
    byte bufPos;                        // Current position in the buffer
    bool overflow = false;              // too long, dropped
    char *last;                        // State variable used by strtok_r during processing
};

extern SerialCommand serialcmd;
#endif
