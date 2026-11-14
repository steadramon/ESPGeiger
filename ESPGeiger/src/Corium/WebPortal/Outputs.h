/*
  Outputs.h - the /outputs body, each module's status as one JSON object.

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

#ifndef CORIUM_OUTPUTS_H
#define CORIUM_OUTPUTS_H

#include <stddef.h>
#include <stdint.h>

namespace corium {
namespace Outputs {

// Writes module i's "a":{..}, 0 = none or it won't fit
size_t module(uint8_t i, char* buf, size_t cap);

// "a":{..},"b":{..} without the outer braces. One that won't fit is dropped
size_t json(char* buf, size_t cap);

}
}  // namespace corium

#endif
