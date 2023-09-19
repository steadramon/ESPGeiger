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
#include <SoftwareSerial.h>

SoftwareSerial sSerial(3, 1);

SerialOut::SerialOut() {
}

void SerialOut::begin() {
  //sSerial.begin(115200);
}

void SerialOut::loop() {
  sSerial.write(PSTR("CPM: %d\n"), gcounter.get_cpm());
#ifdef SERIALOUT_WEB
  Log::console(PSTR("CPM: %d"), gcounter.get_cpm());
#else
  Serial.printf (PSTR ("CPM: %d\n"), gcounter.get_cpm());
#endif
}
#endif