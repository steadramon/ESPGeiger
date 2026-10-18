/*
  OutputVars.h - Central registry of variables exposed to output channels.

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
#ifndef OUTPUTVARS_H
#define OUTPUTVARS_H

#include <stdint.h>
#include <stddef.h>
#include "EGMeasure.h"

namespace OutputVars {

// key not NUL-terminated, may carry :spec. 0 = unknown or bad spec
size_t render(const char* key, size_t keylen, char* buf, size_t cap);

// {name} {name:F1} \n \r \t \\ \{ \}, unknown left as written. tpl in SRAM
size_t renderTemplate(const char* tpl, char* buf, size_t cap);

}  // namespace OutputVars

#endif
