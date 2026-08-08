/*
  AsyncHTTPAnchor.cpp - single translation unit for AsyncHTTPRequest_Generic.

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

// AsyncHTTPRequest_Generic keeps its implementation in the .h; modules include
// the declarations-only .hpp. Exactly one project source must include the .h,
// both as the PlatformIO LDF anchor and as the single point of instantiation.
//
// Kept in its own TU so ~10 KB of HTTP client code is not laid out beside
// loop() and sTickerCB(). Do not move this include into a file with tick-path
// code.

#include "AsyncHTTPRequest_Generic.h"
