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

#ifndef CORIUM_IMPORTTEXT_H
#define CORIUM_IMPORTTEXT_H

#include <stddef.h>
#include <stdint.h>
#include "App/AppConfig.h"
#include "Corium/Prefs/Prefs.h"

// export blob head and its base64, set both; app sets its own
#ifndef CORIUM_EXPORT_MAGIC
#define CORIUM_EXPORT_MAGIC     0x08, 0xE4, 0x48
#define CORIUM_EXPORT_MAGIC_B64 "CORI"
#endif
#ifndef CORIUM_EXPORT_SEED
#define CORIUM_EXPORT_SEED 0x434F5249u   // xor keystream seed, also on the crc
#endif

namespace corium {
namespace ImportText {

// false = skipped
typedef bool (*PutFn)(const char* mid, const char* key, const char* value);

// firstSkip: line number from 1, 0 = none; firstKey: a blob's first refused mid.key, "" = none
struct Result {
  int applied;
  int skipped;
  int firstSkip;
  char firstKey[2 * CORIUM_PREF_ID_MAX + 2];
};

// export blobs always start CORIUM_EXPORT_MAGIC_B64; past leading blanks, nullptr = not one
const char* blobStart(const char* text, size_t len);

// # comments and blank lines ignored
Result apply(const char* text, size_t len, PutFn put);

// records: group count, per group len+mid and pref count, per pref len+key, be16 len+value
// blob records from the group count on, all of len; nullptr = ok, else why (PROGMEM)
const char* blobError(const uint8_t* p, size_t len);
// applied -1 = bad records; a refused put counts in skipped
Result applyBlob(const uint8_t* p, size_t len, PutFn put);

}
}  // namespace corium

#endif
