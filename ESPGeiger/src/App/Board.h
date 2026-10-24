/*
  Board.h - which revision of the board this is, read once at boot.

  Copyright (C) 2026 @steadramon

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
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>
#ifdef ESPGEIGER_HW32
#include "Corium/Logger/Logger.h"
#include "Corium/System/PinProbe.h"
#include "Corium/Output/Buzzer.h"
#endif

namespace Board {

#ifdef ESPGEIGER_HW32
  inline uint8_t& rev_slot() { static uint8_t r = 0; return r; }
  inline uint8_t rev() { return rev_slot(); }

  // after the boot button is let go
  inline void detect() {
    // the button pull-up is v2 only, a misread says v1, the gentle one
    rev_slot() = PinProbe::pulled(PUSHBUTTON_PIN) == PinProbe::UP ? 2 : 1;
    pinMode(PUSHBUTTON_PIN, INPUT_PULLUP);
    Log::console(PSTR("Board: v%u"), (unsigned)rev_slot());
  }

  // v1 drives the coil DC: 25 %, 2 s then rest. v2 is AC coupled
  inline Buzzer::Limits buzzer() {
    Buzzer::Limits l = Buzzer::DEFAULTS;
    l.duty_pct = rev() == 2 ? 50 : 25;
    l.run_ms = rev() == 2 ? 0 : 2000;
    l.break_ms = 20;
    l.rest_ms = 2000;
    return l;
  }
#else
  // one revision only
  inline uint8_t rev() { return 0; }
  inline void detect() {}
#endif

}

#endif
