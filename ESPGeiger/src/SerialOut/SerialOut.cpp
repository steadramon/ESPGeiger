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
  Serial.printf (PSTR ("CPM: %d\r\n"), gcounter.get_cpm());
}

void SerialOut::print_usv() {
  Serial.printf (PSTR ("uSv: %.3f\r\n"), gcounter.get_usv());
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
      Serial.printf (PSTR ("CPM: %d"), gcounter.get_cpm());
      if (_show_cps) {
        Serial.printf (PSTR (", CPS: %.3f"), gcounter.get_cps());
      }
      if (_show_usv) {
        Serial.printf (PSTR (", uSv: %.3f"), gcounter.get_usv());
      }
#ifdef ESPGEIGER_HW
      if (_show_hv) {
        Serial.printf (PSTR (", HV: %.3f"), status.hvReading.get());
      }
#endif
      Serial.printf (PSTR ("\r\n"));
    }
  }
}
#endif