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
#include "../../Util/StringUtil.h"
#include "../../Prefs/EGPrefs.h"

static EspSoftwareSerial::UART geigerPort;

static const uint32_t SERIAL_BAUD[] = { 9600, 115200, 9600, 115200 };

GeigerTestSerial::GeigerTestSerial() {
  strcpy(_test_type, "TestSerial");
};

void GeigerTestSerial::begin() {
  uint8_t st = (uint8_t)EGPrefs::getUInt("input", "serial_type");
  if (st >= 1 && st <= 4) _serial_type = st;
  uint32_t baud = SERIAL_BAUD[_serial_type - 1];
  Log::console(PSTR("TestSerial: type %d baud %lu rx %d tx %d"), _serial_type, baud, _rx_pin, _tx_pin);
#ifdef GEIGER_COUNT_TXPULSE
  Log::console(PSTR("TestSerial: TX pulse counting enabled"));
#endif
  geigerPort.begin(baud, SWSERIAL_8N1, _rx_pin, _tx_pin, false, 16);
  serialAvg.begin(SMOOTHED_AVERAGE, 16);
  CPMAdjuster();
}

void GeigerTestSerial::pullSerial() {
  while (geigerPort.available()) {
    char input = geigerPort.read();
#ifdef ESP8266
    ESP.wdtFeed();
#endif
    delay(1);
    _serial_buffer[_serial_idx++] = input;
    if (input == '\n') {
      _serial_buffer[_serial_idx++] = '\0';
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

void GeigerTestSerial::loop() {
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
  if (millis() - last_serial > 10000) {
    serial_value = 0;
  }
}

void GeigerTestSerial::secondTicker() {
  CPMAdjuster();
  _poisson_target = 60.0 / (GeigerInputTest::getTargetCPS() * 60.0);
  double poisson_result = generatePoissonRand(_poisson_target);
  serialAvg.add(poisson_result);
  int test_serialCPM = 60 * serialAvg.get();

  test_partial_clicks += poisson_result;
  int test_serialCPS = 0;
  if (test_partial_clicks >= 1.0) {
    int full_clicks = (int)test_partial_clicks;
    test_partial_clicks -= full_clicks;
    test_serialCPS = full_clicks;
#ifdef GEIGER_COUNT_TXPULSE
    setCounter(full_clicks);
#endif
  }

  // Generate output in the selected serial format
  switch (_serial_type) {
    case GEIGER_STYPE_MIGHTYOHM: {
      static constexpr float INV_175 = 1.0f / 175.0f;
      float test_serialuSV = serialAvg.get() * 60.0f * INV_175;
      char usv_str[12];
      format_f(usv_str, sizeof(usv_str), test_serialuSV);
      Log::debug(PSTR("TestSerial: CPS, %d, CPM, %d, uSv/hr, %s, SLOW"), test_serialCPS, test_serialCPM, usv_str);
      geigerPort.printf("CPS, %d, CPM, %d, uSv/hr, %s, SLOW\n", test_serialCPS, test_serialCPM, usv_str);
      break;
    }
    case GEIGER_STYPE_ESPGEIGER:
      Log::debug(PSTR("TestSerial: CPM: %d"), test_serialCPM);
      geigerPort.printf("CPM: %d\n", test_serialCPM);
      break;
    default:
      Log::debug(PSTR("TestSerial: %d"), test_serialCPM);
      geigerPort.printf("%d\r\n", test_serialCPM);
      break;
  }

  delay(10);
#ifdef GEIGER_COUNT_TXPULSE
  return;
#endif
  // All serial types report CPM
  static constexpr float INV_60 = 1.0f / 60.0f;
  partial_clicks += (float)serial_value * INV_60;
  if (partial_clicks >= 1.0f) {
    int full_clicks = (int)partial_clicks;
    partial_clicks -= full_clicks;
    setCounter(full_clicks, false);
  }
}

void GeigerTestSerial::handleSerial(char* input) {
  size_t inputLen = strlen(input);
  for (size_t x = 0; x < inputLen; x++) {
    if (!isPrintable(input[x]) && input[x] != '\r' && input[x] != '\n') {
      Log::debug(PSTR("Non-printable character on serial"));
      return;
    }
  }

  int _scpm = 0;
  int n = 0;

  switch (_serial_type) {
    case GEIGER_STYPE_MIGHTYOHM: {
      int _scps;
      n = sscanf(input, "CPS, %d, CPM, %d", &_scps, &_scpm);
      if (n != 2) return;
      break;
    }
    case GEIGER_STYPE_ESPGEIGER:
      n = sscanf(input, "CPM: %d", &_scpm);
      if (n != 1) return;
      break;
    default:
      for (size_t x = 0; x < inputLen; x++) {
        if (!isDigit(input[x]) && input[x] != '\r' && input[x] != '\n') return;
      }
      n = sscanf(input, "%d", &_scpm);
      if (n != 1) return;
      break;
  }

  Log::debug(PSTR("TestSerial: Loop - %d"), _scpm);
  setLastBlip();
  serial_value = _scpm;
  unsigned long now = millis();
  int diff = now - last_serial;
  if (last_serial != 0) avg_diff = (avg_diff + diff) / 2;
  last_serial = now;
}
