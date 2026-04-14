/*
  SerialOut.cpp - Geiger Counter class
  
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

#include <Arduino.h>
#include "SerialOut.h"
#include "../Logger/Logger.h"

SerialOut::SerialOut() {
}

void SerialOut::print_cpm() {
  char buf[32];
  snprintf(buf, sizeof(buf), "CPM: %d\n", gcounter.get_cpm());
  Serial.print(buf);
}

void SerialOut::print_usv() {
  char buf[32];
  snprintf(buf, sizeof(buf), "uSv: %.2f\n", gcounter.get_usv());
  Serial.print(buf);
}

void SerialOut::toggle_usv() {
  _show_usv = !_show_usv;
}

void SerialOut::toggle_hv() {
  _show_hv = !_show_hv;
}

void SerialOut::toggle_cps() {
  _show_cps = !_show_cps;
}

void SerialOut::set_show(int var) {
  status.serialOut = var;
  if (status.serialOut > 0) {
    Log::setSerialLogLevel(false);
  } else {
    Log::setSerialLogLevel(true);
  }
}

void SerialOut::loop(unsigned long stick_now) {
  if (status.serialOut > 0) {
    _loop_c++;
    if (_loop_c >= status.serialOut) {
      _loop_c = 0;
      char buf[64];
      int pos = snprintf(buf, sizeof(buf), "CPM: %d", gcounter.get_cpm());
      if (_show_cps) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, " CPS: %.2f", gcounter.get_cps());
      }
      if (_show_usv) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, " uSv: %.2f", gcounter.get_usv());
      }
#ifdef ESPGEIGER_HW
      if (_show_hv) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, " HV: %d", (int)status.hvReading.get());
      }
#endif
      Serial.print(buf);
      Serial.print('\n');
    }
  }
}
#endif