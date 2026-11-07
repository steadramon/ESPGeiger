/*
  Attr.h - Compiler attributes, one name for both cores.

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
#ifndef CORIUM_ATTR_H
#define CORIUM_ATTR_H

// a dropped result warns; esp32 is gnu++11, no [[nodiscard]]
#define CORIUM_NODISCARD __attribute__((warn_unused_result))

#endif
