/*
  Tube.h - Tube model and what it detects.

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
#ifndef COUNTER_TUBE_H
#define COUNTER_TUBE_H

#include <stdint.h>
#include "Corium/Platform/Flash.h"

namespace Tube {
  const char* model();
  void set_model(const char* s);   // s may be in flash
  uint8_t detection();   // bits: alpha beta gamma
}

#endif
