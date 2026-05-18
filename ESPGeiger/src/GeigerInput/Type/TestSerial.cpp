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

#if GEIGER_IS_TEST(GEIGER_TYPE)

#include "../../Logger/Logger.h"
#include "../../Util/StringUtil.h"
#include "../../Prefs/EGPrefs.h"
#include "../../Util/DeviceInfo.h"
#include "../../Counter/Counter.h"
#include "../SerialFormat.h"

extern Counter gcounter;

static EspSoftwareSerial::UART geigerPort;

GeigerTestSerial::GeigerTestSerial() {
  strcpy(_test_type, "TestSerial");
};

void GeigerTestSerial::begin() {
  uint8_t st = (uint8_t)EGPrefs::getUInt("input", "serial_type");
  if (SerialFormat::is_known(st)) _serial_type = st;
  uint32_t    baud = SerialFormat::baud_for(_serial_type);
  const char* name = SerialFormat::name_for(_serial_type);
  if (baud == 0) baud = 9600;
  if (name && EGPrefs::getString("input", "geiger_model")[0] == '\0') {
    DeviceInfo::setGeigermodel(name);
  }
  Log::console(PSTR("TestSerial: %s (type %d) BAUD: %lu RXPIN: %d"),
               name ? name : "?", _serial_type, baud, _rx_pin, _tx_pin);
#ifdef GEIGER_COUNT_TXPULSE
  Log::console(PSTR("TestSerial: TX pulse counting enabled"));
#endif
  geigerPort.begin(baud, SWSERIAL_8N1, _rx_pin, _tx_pin, false, 16);
  // 16-byte buffer fills in 160_000_000/baud us; iter ~18us, 6x safety
  // margin. Lower base constant than GeigerSerial (16-byte vs 64-byte).
  uint32_t skip = 1500000UL / baud;
  if (skip < 5)   skip = 5;
  if (skip > 500) skip = 500;
  _poll_skip = (uint16_t)skip;
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
  if (_loop_c < _poll_skip && _serial_idx == 0) {
    _loop_c++;
    return;
  }
  _loop_c = 0;
  pullSerial();
}

void GeigerTestSerial::secondTicker() {
  CPMAdjuster();
  int count = 0;
  double target = (double) GeigerInputTest::getTargetCPS();
  if (target > 0) {
    double u1, u2;
#ifdef ESP8266
    u1 = (RANDOM_REG32 + 1.0) / (0xFFFFFFFF + 2.0);
    u2 = (RANDOM_REG32 + 1.0) / (0xFFFFFFFF + 2.0);
#else
    u1 = (esp_random() + 1.0) / (0xFFFFFFFF + 2.0);
    u2 = (esp_random() + 1.0) / (0xFFFFFFFF + 2.0);
#endif
    double z = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
    long c = (long) (target + sqrt(target) * z + 0.5);
    if (c > 0) count = (int) c;
  }

  // CPS = instant sample, CPM = Counter's smoothed value (matches real
  // GC10 / MightyOhm wire behaviour).
  char line[80];
  size_t n = SerialFormat::format_line(_serial_type, count, gcounter.get_cpm(), line, sizeof(line));
  if (n > 0) {
    char echo[80];
    memcpy(echo, line, n);
    echo[n] = '\0';
    while (n > 0 && (echo[n - 1] == '\n' || echo[n - 1] == '\r')) echo[--n] = '\0';
    Log::console(PSTR("TestSerial TX: %s"), echo);
    geigerPort.write((const uint8_t*) line, strlen(line));
  }

  if (count > 0) setCounter(count, false);
}

void GeigerTestSerial::handleSerial(char* input) {
  int _scpm = 0;
  if (SerialFormat::parse_cpm(_serial_type, input, &_scpm)) {
    Log::console(PSTR("TestSerial RX: %d"), _scpm);
  }
}
#endif // GEIGER_IS_TEST
