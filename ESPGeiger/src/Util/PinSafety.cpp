/*
  PinSafety.cpp - Pin-in-use registry. Diagnostic only.

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

#include "PinSafety.h"
#include "../Logger/Logger.h"

namespace {
  constexpr int MAX_CLAIMS = 12;
  struct PinClaim { int pin; const char* owner; };
  PinClaim s_claims[MAX_CLAIMS];
  int s_count = 0;
}

void PinSafety::claim(int pin, const char* owner) {
  if (pin == -1) return;
  for (int i = 0; i < s_count; i++) {
    if (s_claims[i].pin == pin) {
      // Pointer compare: each module passes its own literal so same module
      // claiming the same pin twice (e.g. on_prefs_saved -> on_prefs_loaded)
      // is silent. Different owner = real conflict.
      if (s_claims[i].owner != owner) {
        Log::console(PSTR("PinSafety: pin=%d claimed by %s, also %s"),
                     pin, s_claims[i].owner, owner);
      }
      return;
    }
  }
  if (s_count < MAX_CLAIMS) {
    s_claims[s_count].pin = pin;
    s_claims[s_count].owner = owner;
    s_count++;
  }
}
