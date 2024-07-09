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
  geigerPort.begin(GEIGER_BAUDRATE, SWSERIAL_8N1, _rx_pin, _tx_pin, false, 16);
}

void GeigerSerial::pullSerial() {
  while (geigerPort.available()) {
    char input = geigerPort.read();
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    delay(1);
    _serial_buffer[_serial_idx++] = input;
    if (input == '\n') {
      handleSerial(_serial_buffer);
      _serial_idx = 0;
      _serial_buffer[0] = '\0';
    }
    if (_serial_idx > 52) {
      _serial_idx = 0;
      _serial_buffer[0] = '\0';
    }
  }
}

void GeigerSerial::loop() {
  if (_loop_c <= 5 && _serial_idx == 0) {
    _loop_c++;
    return;
  }
  _loop_c = 0;
  pullSerial();
  if (avg_diff <= 0) {
    avg_diff = 0;
    return;
  }
  if (serial_value <= 0) {
    serial_value = 0;
    return;
  }
  unsigned long now = millis();
/*
  if (now - last_serial > avg_diff*2) {
    serial_value = serial_value / 2;
  }
*/
  if (now - last_serial > 10000) {
    serial_value = 0;
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
}

void GeigerSerial::handleSerial(char* input) {
  int _scpm = 0;
#if GEIGER_SERIALTYPE == GEIGER_STYPE_MIGHTYOHM
  int _scps;
  int n = sscanf(input, "CPS, %d, CPM, %d", &_scps, &_scpm);
  if (n == 2) {
#elif GEIGER_SERIALTYPE == GEIGER_STYPE_ESPGEIGER
  int n = sscanf(input, "CPM: %d", &_scpm);
  if (n == 1) {
#else
  for (int x = 0; x < strlen(input); x++) {
    if (!isDigit(input[x]) && (input[x] != '\r') && (input[x] != '\n')) {
      return;
    }
  }
  int n = sscanf(input, "%d\r\n", &_scpm);
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