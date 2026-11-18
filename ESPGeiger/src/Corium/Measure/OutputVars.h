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
#ifndef CORIUM_OUTPUTVARS_H
#define CORIUM_OUTPUTVARS_H

#include <stdint.h>
#include <stddef.h>
#include "Measure.h"

namespace corium {
namespace OutputVars {

// key isn't NUL terminated and may end in :spec
size_t render(const char* key, size_t klen, char* buf, size_t cap, bool* cut = nullptr);

// Fills in {name} {name:F1} and the escapes \n \r \t \\ \{ \}, anything unknown is left as written. tpl in SRAM
size_t renderTemplate(const char* tpl, char* buf, size_t cap);

}  // namespace OutputVars
}  // namespace corium

#endif
