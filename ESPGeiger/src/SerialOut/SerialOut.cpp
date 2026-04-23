/*
  SerialOut.cpp - Serial Out class
  
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
#include "../Module/EGModuleRegistry.h"
#include "../Util/StringUtil.h"
#ifdef ESPGEIGER_HW
#include "../ESPGHW/ESPGHW.h"
#endif

SerialOut serialout;
EG_REGISTER_MODULE(serialout)

SerialOut::SerialOut() {
}

void SerialOut::print_cpm() {
  char buf[32];
  snprintf_P(buf, sizeof(buf), PSTR("CPM: %d\n"), gcounter.get_cpm());
  Serial.print(buf);
}

void SerialOut::print_usv() {
  char buf[32];
  char f[12];
  format_f(f, sizeof(f), gcounter.get_usv());
  snprintf_P(buf, sizeof(buf), PSTR("uSv: %s\n"), f);
  Serial.print(buf);
}

void SerialOut::set_show(int var) {
  if (var < 0) var = 0;
  if (var > UINT16_MAX) var = UINT16_MAX;
  _interval = (uint16_t)var;
  if (_interval == 0) {
    _show_flags = 0;
  } else {
    _show_flags |= SHOW_CPM;
  }
  Log::setSerialLogLevel(_show_flags == 0);
}

void SerialOut::toggle_cpm() { _show_flags ^= SHOW_CPM; Log::setSerialLogLevel(_show_flags == 0); }
void SerialOut::toggle_usv() { _show_flags ^= SHOW_USV; Log::setSerialLogLevel(_show_flags == 0); }
void SerialOut::toggle_hv()  { _show_flags ^= SHOW_HV;  Log::setSerialLogLevel(_show_flags == 0); }
void SerialOut::toggle_cps() { _show_flags ^= SHOW_CPS; Log::setSerialLogLevel(_show_flags == 0); }

void SerialOut::loop(unsigned long now) {
  if (_show_flags == 0) return;
  if (_interval == 0) return;
  if ((now - _last_fire) < (uint32_t)_interval * 1000UL) return;
  _last_fire = now;

  char buf[80];
  char f[12];
  size_t pos = 0;
  int n;
  if (_show_flags & SHOW_CPM) {
    n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR("CPM: %d"), gcounter.get_cpm());
    advance_pos(pos, n, sizeof(buf));
  }
  if (_show_flags & SHOW_CPS) {
    format_f(f, sizeof(f), gcounter.get_cps());
    n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR("%sCPS: %s"), pos ? " " : "", f);
    advance_pos(pos, n, sizeof(buf));
  }
  if (_show_flags & SHOW_USV) {
    format_f(f, sizeof(f), gcounter.get_usv());
    n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR("%suSv: %s"), pos ? " " : "", f);
    advance_pos(pos, n, sizeof(buf));
  }
#ifdef ESPGEIGER_HW
  if (_show_flags & SHOW_HV) {
    n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR("%sHV: %d"), pos ? " " : "", (int)hardware.hvReading.get());
    advance_pos(pos, n, sizeof(buf));
  }
#endif
  if (pos == 0) return;
  if (pos < sizeof(buf) - 1) buf[pos++] = '\n';
  Serial.write((const uint8_t*)buf, pos);
}
#endif
