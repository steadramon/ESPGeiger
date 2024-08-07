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

extern Counter gcounter;

class SerialOut {
    public:
      SerialOut();
      void loop(unsigned long stick_now);
      void set_show(int var);
      void print_cpm();
      void print_usv();
      void toggle_usv();
      void toggle_hv();
      void toggle_cps();
    private:
      int _loop_c = 0;
      bool _show_usv;
      bool _show_hv;
      bool _show_cps;
};

#endif
#endif