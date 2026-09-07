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

#if GEIGER_IS_TEST(GEIGER_TYPE) && GEIGER_IS_SERIAL(GEIGER_TYPE)

#include <SoftwareSerial.h>
#include "../../Corium/Logger/Logger.h"
#include "../../Corium/Util/StringUtil.h"
#include "../../Corium/Prefs/EGPrefs.h"
#include "../../GeigerPrefs/GeigerKeys.h"
#include "../../Corium/System/DeviceInfo.h"
#include "../../Counter/Counter.h"
#include "../../Counter/Tube.h"
#include "../SerialFormat.h"

static GeigerPort<16> geigerPort;

GeigerTestSerial::GeigerTestSerial() {
  strlcpy(_test_type, "TestSerial", sizeof(_test_type));
};

void GeigerTestSerial::begin() {
  uint8_t st = (uint8_t)EGPrefs::getUInt("counter", EG_K_SERIAL_TYPE);
  if (SerialFormat::is_known(st)) _serial_type = st;
  uint32_t    baud = SerialFormat::baud_for(_serial_type);
  const char* name = SerialFormat::name_for(_serial_type);
  if (baud == 0) baud = 9600;
  if (name && EGPrefs::getString("counter", EG_K_GEIGER_MODEL)[0] == '\0') {
    Tube::set_model(name);
  }
  Log::console(PSTR("TestSerial: %s (type %d) BAUD: %lu RXPIN: %d"),
               name ? name : PSTR("?"), _serial_type, baud, _rx_pin, _tx_pin);
#ifdef GEIGER_COUNT_TXPULSE
  Log::console(PSTR("TestSerial: TX pulse counting enabled"));
#endif
  geigerPort.begin(baud, _rx_pin, _tx_pin);
  uint32_t skip = 1500000UL / baud;
  if (skip < 5)   skip = 5;
  if (skip > 500) skip = 500;
  _idle_skip = (uint16_t)skip;
  _poll_skip = _idle_skip;
  CPMAdjuster();
}

void GeigerTestSerial::pullSerial() {
  while (geigerPort.available()) {
    char input = geigerPort.read();
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
  pullSerial();
  _poll_skip = _serial_idx ? 0 : _idle_skip;   // mid line, every pass
}

void GeigerTestSerial::secondTicker() {
  CPMAdjuster();
  int count = 0;
  double target = (double) GeigerInputTest::getTargetCPS();
  if (target > 0) {
    double u1, u2;
    u1 = ((double)DeviceInfo::hwRandom() + 1) / ((double)0xFFFFFFFF + 2);
    u2 = ((double)DeviceInfo::hwRandom() + 1) / ((double)0xFFFFFFFF + 2);
    double z = sqrt(-2 * log(u1)) * cos(2 * M_PI * u2);
    long c = lround(target + sqrt(target) * z);
    if (c > 0) count = (int) c;
  }

  // count first, like the device
  if (count > 0) setCounter(count, false);

  char line[80];
  size_t n = SerialFormat::format_line(_serial_type, line, sizeof(line));
  if (n > 0) {
    char echo[80];
    memcpy(echo, line, n);
    echo[n] = '\0';
    while (n > 0 && (echo[n - 1] == '\n' || echo[n - 1] == '\r')) echo[--n] = '\0';
    Log::console(PSTR("TestSerial TX: %s"), echo);
    geigerPort.write((const uint8_t*) line, strlen(line));
  }
}

void GeigerTestSerial::handleSerial(char* input) {
  int _scpm = 0;
  if (SerialFormat::parse_cpm(_serial_type, input, &_scpm)) {
    Log::console(PSTR("TestSerial RX: %d"), _scpm);
  }
}
#endif // GEIGER_IS_TEST && GEIGER_IS_SERIAL
