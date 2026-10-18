/*
  prod_serialout.cpp - the real SerialOut.cpp, built against the fakes.

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

#include "sout_fakes.h"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "SerialOut/SerialOut.cpp"
#include "Corium/Util/Words.cpp"
#include "../../lib/EGText/src/EGFormat.cpp"
#include "../../lib/EGHttp/src/EGHttpStrings.cpp"
