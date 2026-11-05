/*
  BootButton.h - Button held at boot: offline, or factory reset if held long.

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
#ifndef CORIUM_BOOT_BUTTON_H
#define CORIUM_BOOT_BUTTON_H

#include <stdint.h>

#ifndef CORIUM_BOOT_BUTTON_RESET_S
#define CORIUM_BOOT_BUTTON_RESET_S 5
#endif

// Held this long = stuck button or a low pin, carry on booting
#ifndef CORIUM_BOOT_BUTTON_STUCK_S
#define CORIUM_BOOT_BUTTON_STUCK_S 30
#endif

namespace corium {
namespace BootButton {
  enum class Hold : uint8_t { NONE = 0, OFFLINE = 1, FULL_RESET = 2 };

  // Blocks until released, NONE when stuck. left() gets the seconds to a reset, 0 = armed
  Hold check(bool (*pressed)(), void (*held)(), void (*left)(int s));
}
}  // namespace corium

#endif
