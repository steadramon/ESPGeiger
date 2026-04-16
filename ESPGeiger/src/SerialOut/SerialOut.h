/*
  SerialOut.h - Serial Output class
  
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

#ifndef SERIALOUT_H
#define SERIALOUT_H
#ifdef SERIALOUT
#include <Arduino.h>
#include "../Status.h"
#include "../Counter/Counter.h"
#include "../Module/EGModule.h"

extern Counter gcounter;

class SerialOut : public EGModule {
    public:
      // Optional extras appended to the always-shown CPM line. Room in the
      // byte for future flags (e.g. SHOW_CPM for a quiet mode).
      static constexpr uint8_t SHOW_USV = 1 << 0;
      static constexpr uint8_t SHOW_HV  = 1 << 1;
      static constexpr uint8_t SHOW_CPS = 1 << 2;

      SerialOut();
      const char* name() override { return "serout"; }
      uint16_t warmup_seconds() override { return 0; }
      bool has_tick() override { return true; }
      void s_tick(unsigned long stick_now) override;
      void set_show(int var);
      void print_cpm();
      void print_usv();
      void toggle_usv() { _show_flags ^= SHOW_USV; }
      void toggle_hv()  { _show_flags ^= SHOW_HV; }
      void toggle_cps() { _show_flags ^= SHOW_CPS; }
    private:
      uint16_t _loop_c = 0;
      uint8_t _show_flags = 0;
};

#endif
#endif