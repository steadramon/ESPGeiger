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
#include "../Module/EGModuleRegistry.h"

SerialOut serialout;
EG_REGISTER_MODULE(serialout)

SerialOut::SerialOut() {
}

void SerialOut::print_cpm() {
  char buf[32];
  snprintf(buf, sizeof(buf), "CPM: %d\n", gcounter.get_cpm());
  Serial.print(buf);
}

void SerialOut::print_usv() {
  char buf[32];
  char f[12];
  format_f(f, sizeof(f), gcounter.get_usv());
  snprintf(buf, sizeof(buf), "uSv: %s\n", f);
  Serial.print(buf);
}

void SerialOut::set_show(int var) {
  // Clamp to uint16_t range. Negative → 0 (off); > 65535 → 65535 (~18h).
  if (var < 0) var = 0;
  if (var > UINT16_MAX) var = UINT16_MAX;
  status.serialOut = (uint16_t)var;
  Log::setSerialLogLevel(status.serialOut == 0);
}

void SerialOut::s_tick(unsigned long stick_now) {
  if (status.serialOut == 0) return;
  if (++_loop_c < status.serialOut) return;
  _loop_c = 0;

  char buf[80];
  char f[12];
  int pos = snprintf(buf, sizeof(buf), "CPM: %d", gcounter.get_cpm());
  if (_show_flags & SHOW_CPS) {
    format_f(f, sizeof(f), gcounter.get_cps());
    pos += snprintf(buf + pos, sizeof(buf) - pos, " CPS: %s", f);
  }
  if (_show_flags & SHOW_USV) {
    format_f(f, sizeof(f), gcounter.get_usv());
    pos += snprintf(buf + pos, sizeof(buf) - pos, " uSv: %s", f);
  }
#ifdef ESPGEIGER_HW
  if (_show_flags & SHOW_HV) {
    pos += snprintf(buf + pos, sizeof(buf) - pos, " HV: %d", (int)status.hvReading.get());
  }
#endif
  if (pos < (int)sizeof(buf) - 1) buf[pos++] = '\n';
  Serial.write((const uint8_t*)buf, pos);
}
#endif