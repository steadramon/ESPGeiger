/*
  GeigerInput/Type/TestSerial.cpp - Class for Test Serial type counter
  
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
#include "TestSerial.h"
#include "../../Logger/Logger.h"

GeigerTestSerial::GeigerTestSerial() {
};

void GeigerTestSerial::begin() {
  Log::console(PSTR("TestSerial: Setting up test %s serial geiger ..."), GEIGER_MODEL);
  Log::console(PSTR("TestSerial: RXPIN: %d BAUD: %d"), _rx_pin, GEIGER_BAUDRATE);
  Log::console(PSTR("TestSerial: TXPIN: %d BAUD: %d"), _tx_pin, GEIGER_BAUDRATE);
  geigerPort.begin(GEIGER_BAUDRATE, SWSERIAL_8N1, _rx_pin, _tx_pin, false);
  serialAvg.begin(SMOOTHED_AVERAGE, 32);
  setTargetCPS(GEIGER_TEST_INITIAL_CPS);
}

void GeigerTestSerial::loop() {
  if (_loop_c <= 5 && _serial_idx == 0) {
    _loop_c++;
    return;
  }
  _loop_c = 0;
  if (geigerPort.available() > 0) {
    if (geigerPort.overflow()) {
      Log::console(PSTR("TestSerial: Serial Overflow %d"), geigerPort.available());
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

void GeigerTestSerial::setTargetCPM(float target) {
  setTargetCPS(target/60.0);
};

void GeigerTestSerial::setTargetCPS(float target) {
  _poisson_target = 60.0/(target*60.0);
};

void GeigerTestSerial::CPMAdjuster() {
#ifndef GEIGER_TESTPULSE_FIXEDCPM
  int selection = (millis() / GEIGER_TESTPULSE_ADJUSTTIME) % 4;
  if (selection != _current_selection) {
    _current_selection = selection;
    int _targetCPM = 30;
    switch (selection) {
      case 1:
        _targetCPM = 60;
        break;
      case 2:
        _targetCPM = 100;
        break;
      case 3:
        _targetCPM = 120;
        break;
      default:
        _targetCPM = 30;
        break;
    }
    Log::console(PSTR("TestSerial: Setting CPM to: %d"), _targetCPM);
    setTargetCPM(_targetCPM);
  }
#endif
}

void GeigerTestSerial::secondTicker() {
  CPMAdjuster();
  double poisson_result = generatePoissonRandom(_poisson_target);
  serialAvg.add(poisson_result);
  int test_serialCPM = 60 * serialAvg.get();

  test_partial_clicks += poisson_result;
  int test_serialCPS = 0;
  if (test_partial_clicks >= 1.0) {
    int full_clicks = (int)test_partial_clicks;
    test_partial_clicks = test_partial_clicks - full_clicks;
    test_serialCPS = full_clicks;
#ifdef GEIGER_COUNT_TXPULSE
    setCounter(full_clicks);
#endif
  }
#if GEIGER_SERIALTYPE == GEIGER_STYPE_MIGHTYOHM
  float test_serialuSV = (serialAvg.get() * 60.0)/175;
  Log::debug(PSTR("TestSerial: CPS, %d, CPM, %d, uSv/hr, %.2lf, SLOW"), test_serialCPS, test_serialCPM, test_serialuSV);
  geigerPort.printf(PSTR("CPS, %d, CPM, %d, uSv/hr, %.2lf, SLOW\n"), test_serialCPS, test_serialCPM, test_serialuSV);
#else
  Log::debug(PSTR("TestSerial: %d"), test_serialCPM);
  geigerPort.printf(PSTR("%d\n"), test_serialCPM);
#endif
  delay(10);
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

void GeigerTestSerial::handleSerial(char* input) {
  int _scpm = 0;
#if GEIGER_SERIALTYPE == GEIGER_STYPE_MIGHTYOHM
  int _scps;
  int n = sscanf(input, "CPS, %d, CPM, %d", &_scps, &_scpm);
  if (n == 2) {
#else
  int n = sscanf(input, "%d\n", &_scpm);
  if (n == 1) {
#endif
    Log::debug(PSTR("TestSerial: Loop - %d"), _scpm);
    setLastBlip();
    serial_value = _scpm;
    unsigned long temptime = millis();
    int diff = temptime - last_serial;
    if (last_serial != 0)
      avg_diff = (avg_diff+diff)/2;
    last_serial = temptime;
  }
}