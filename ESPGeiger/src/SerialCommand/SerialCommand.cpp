/*
  SerialCommand.cpp - Geiger Counter class
  
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
extern SerialCommand serialcmd;

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
  addCommand(PSTR("ratio"), set_ratio);
#ifdef SERIALOUT
  addCommand(PSTR("cpm"), get_cpm);
  addCommand(PSTR("usv"), get_usv);
  addCommand(PSTR("show"), set_show);
#endif
#ifdef GEIGERTESTMODE
  addCommand(PSTR("target"), set_cpm);
#endif
#ifdef ESPGEIGER_HW
  addCommand(PSTR("hv"), get_hv);
  addCommand(PSTR("duty"), set_duty);
  addCommand(PSTR("freq"), set_freq);
  addCommand(PSTR("vratio"), set_vdratio);
  addCommand(PSTR("voffset"), set_vdoffset);
#endif
}

void SerialCommand::addCommand(const char *command, void (*function)()) {
  #ifdef SERIALCOMMAND_DEBUG
    Serial.print("Adding command (");
    Serial.print(commandCount);
    Serial.print("): ");
    Serial.println(command);
  #endif

  commandList = (SerialCommandCallback *) realloc(commandList, (commandCount + 1) * sizeof(SerialCommandCallback));
  strncpy(commandList[commandCount].command, command, SERIALCOMMAND_MAXCOMMANDLENGTH);
  commandList[commandCount].function = function;
  commandCount++;
}
#ifdef SERIALOUT
void SerialCommand::set_show() {
  char *arg;
  int aNumber;
  arg = strtok_r(NULL, serialcmd.delim, &serialcmd.last);
  if (arg != NULL) {
    if (strstr("usv", arg)) {
      serialout.toggle_usv();
      return;
    }
    if (strstr("hv", arg)) {
      serialout.toggle_hv();
      return;
    }
    if (strstr("cps", arg)) {
      serialout.toggle_cps();
      return;
    }
    if (strstr("off", arg)) {
      serialout.set_show(0);
      return;
    }
    if (isDigit(arg[0])) {
      aNumber = atoi(arg);
      serialout.set_show(aNumber);
    }
  } else {
    serialout.set_show(1);
    serialout.loop(1);
    serialout.set_show(0);
  }
}

void SerialCommand::get_cpm() {
  serialout.print_cpm();
}

void SerialCommand::get_usv() {
  serialout.print_usv();
}

#endif
void SerialCommand::set_ratio() {
  char *arg;
  float aNumber;
  arg = strtok_r(NULL, serialcmd.delim, &serialcmd.last);
  if (arg != NULL) {
    aNumber = atof(arg);    // Converts a char string to an integer
    if (aNumber != NULL) {
      gcounter.set_ratio(aNumber);
    }
  } else {
    Serial.printf (PSTR ("uSv Ratio: %.2f\r\n"), gcounter.get_ratio());
  }
}
#ifdef GEIGERTESTMODE
void SerialCommand::set_cpm() {
  char *arg;
  int aNumber;
  arg = strtok_r(NULL, serialcmd.delim, &serialcmd.last);
  if (arg != NULL) {
    aNumber = atoi(arg);    // Converts a char string to an integer
    if (aNumber != NULL) {
      gcounter.set_target_cpm(aNumber);
    }
  }
}
#endif
#ifdef ESPGEIGER_HW
void SerialCommand::get_hv() {
  Serial.printf (PSTR ("HV: %.3f\r\n"), status.hvReading.get());
}

void SerialCommand::set_freq() {
  char *arg;
  int aNumber;
  arg = strtok_r(NULL, serialcmd.delim, &serialcmd.last);
  if (arg != NULL) {
    aNumber = atoi(arg);    // Converts a char string to an integer
    if (aNumber != NULL) {
      hardware.set_freq(aNumber);
      hardware.saveconfig();
    }
  } else {
    Serial.printf (PSTR ("Freq: %d\r\n"), hardware.get_freq());
  }
}

void SerialCommand::set_duty() {
  char *arg;
  int aNumber;
  arg = strtok_r(NULL, serialcmd.delim, &serialcmd.last);
  if (arg != NULL) {
    aNumber = atoi(arg);    // Converts a char string to an integer
    if (aNumber != NULL) {
      hardware.set_duty(aNumber);
      hardware.saveconfig();
    }
  } else {
    Serial.printf (PSTR ("Duty: %d\r\n"), hardware.get_duty());
  }
}

void SerialCommand::set_vdratio() {
  char *arg;
  int aNumber;
  arg = strtok_r(NULL, serialcmd.delim, &serialcmd.last);
  if (arg != NULL) {
    aNumber = atoi(arg);    // Converts a char string to an integer
    if (aNumber != NULL) {
      hardware.set_vd_ratio(aNumber);
      hardware.saveconfig();
    }
  } else {
    Serial.printf (PSTR ("VD Ratio: %d\r\n"), hardware.get_vd_ratio());
  }
}

void SerialCommand::set_vdoffset() {
  char *arg;
  int aNumber;
  arg = strtok_r(NULL, serialcmd.delim, &serialcmd.last);
  if (arg != NULL) {
    aNumber = atoi(arg);    // Converts a char string to an integer
    if (aNumber != NULL) {
      hardware.set_vd_offset(aNumber);
      hardware.saveconfig();
    }
  } else {
    Serial.printf (PSTR ("VD Offset: %d\r\n"), hardware.get_vd_offset());
  }
}
#endif

void SerialCommand::readSerial() {
  while (Serial.available() > 0) {
    char inChar = Serial.read();   // Read single available character, there may be more waiting
    #ifdef SERIALCOMMAND_DEBUG
      Serial.print(inChar);   // Echo back to serial stream
    #endif

    if (inChar == term) {     // Check for the terminator (default '\r') meaning end of command
      #ifdef SERIALCOMMAND_DEBUG
        Serial.print("Received: ");
        Serial.println(buffer);
      #endif
      char *command = strtok_r(buffer, delim, &last);   // Search for command at start of buffer
      if (command != NULL) {
        boolean matched = false;
        for (int i = 0; i < commandCount; i++) {
          #ifdef SERIALCOMMAND_DEBUG
            Serial.print("Comparing [");
            Serial.print(command);
            Serial.print("] to [");
            Serial.print(commandList[i].command);
            Serial.println("]");
          #endif

          // Compare the found command against the list of known commands for a match
          if (strncmp(command, commandList[i].command, SERIALCOMMAND_MAXCOMMANDLENGTH) == 0) {
            #ifdef SERIALCOMMAND_DEBUG
              Serial.print("Matched Command: ");
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
    else if (isprint(inChar)) {     // Only printable characters into the buffer
      if (bufPos < SERIALCOMMAND_BUFFER) {
        buffer[bufPos++] = inChar;  // Put character into buffer
        buffer[bufPos] = '\0';      // Null terminate
      } else {
        #ifdef SERIALCOMMAND_DEBUG
          Serial.println("Line buffer is full - increase SERIALCOMMAND_BUFFER");
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