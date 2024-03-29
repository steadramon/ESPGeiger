/*
  GeigerInput/Type/Serial.cpp - Class for Serial type counter
  
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
#include "Serial.h"
#include "../../Logger/Logger.h"

GeigerSerial::GeigerSerial() {
};

void GeigerSerial::begin() {
  Log::console(PSTR("GeigerSerial: Setting up %s serial geiger ..."), GEIGER_MODEL);
  Log::console(PSTR("GeigerSerial: RXPIN: %d BAUD: %d"), _rx_pin, GEIGER_BAUDRATE);
  geigerPort.begin(GEIGER_BAUDRATE, SWSERIAL_8N1, _rx_pin, _tx_pin, false);
}

void GeigerSerial::loop() {
  if (geigerPort.available() > 0) {
    if (geigerPort.overflow()) {
      Log::console(PSTR("GeigerSerial: Serial Overflow %d"), geigerPort.available());
    }
    optimistic_yield(100 * 1000);
    char input = geigerPort.read();
    optimistic_yield(100 * 1000);
    _serial_buffer[_serial_idx++] = input;
    if (input == '\n') {
      handleSerial(_serial_buffer);
      _serial_idx = 0;
    }

    if (_serial_idx > 52) {
      _serial_idx = 0;
    }
  }
}

void GeigerSerial::secondTicker() {
#if GEIGER_SERIAL_TYPE == GEIGER_SERIAL_CPM
  partial_clicks += (float)serial_value/(float)60;
  if (partial_clicks >= 1.0) {
    int full_clicks = (int)partial_clicks;
    partial_clicks = partial_clicks - full_clicks;
    setCounter(full_clicks, false);
  }
#else
  setCounter(serial_value, false);
#endif
  if (avg_diff < 0) {
    return;
  }
  unsigned long now = millis();
  if (now - last_serial > avg_diff*2) {
    serial_value = serial_value / 2;
  }
}

void GeigerSerial::handleSerial(char* input) {
  int _scpm = 0;
#if GEIGER_SERIALTYPE == GEIGER_STYPE_MIGHTYOHM
  int _scps;
  int n = sscanf(input, "CPS, %d, CPM, %d", &_scps, &_scpm);
  if (n == 2) {
#else
  int n = sscanf(input, "%d\n", &_scpm);
  if (n == 1) {
#endif
    Log::debug(PSTR("GeigerSerial: Loop - %d"), _scpm);
    setLastBlip();
    serial_value = _scpm;
    unsigned long temptime = millis();
    int diff = temptime - last_serial;
    if (last_serial != 0)
      avg_diff = (avg_diff+diff)/2;
    last_serial = temptime;
  }
}