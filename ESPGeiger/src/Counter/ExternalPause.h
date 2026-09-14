/*
  ExternalPause.h - auto-expiring pause on external posters.

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

#ifndef _ESPG_EXTERNAL_PAUSE_H
#define _ESPG_EXTERNAL_PAUSE_H

#include <stdint.h>

namespace ExternalPause {
  // 0 ms = resume now.
  void     start(uint32_t timeout_ms);
  // Not a pure read: clears and logs on expiry.
  bool     active();
  uint32_t remaining_ms();
}

#endif
