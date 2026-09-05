/*
  ImportText.h - plain text config import, module.key=value per line.

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

#ifndef IMPORTTEXT_H
#define IMPORTTEXT_H

#include <stddef.h>
#include <stdint.h>

namespace ImportText {

// false = skipped
typedef bool (*PutFn)(const char* mid, const char* key, const char* value);

// first_skip: line number from 1, 0 = none
struct Result { int applied; int skipped; int first_skip; };

// export blobs always start ESPG
bool is_blob(const char* text, size_t len);

// # comments and blank lines ignored
Result apply(const char* text, size_t len, PutFn put);

// blob records from the group count on; nullptr = ok, else why (PROGMEM)
const char* blob_error(const uint8_t* p, size_t len);
// puts done, -1 = bad records
int apply_blob(const uint8_t* p, size_t len, PutFn put);

}

#endif
