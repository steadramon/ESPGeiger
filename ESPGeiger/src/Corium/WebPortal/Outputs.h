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

namespace corium {
namespace Outputs {

// "a":{..},"b":{..} without the braces; one that won't fit is dropped
size_t json(char* buf, size_t cap, unsigned long now);

}
}  // namespace corium

#endif
