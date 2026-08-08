/*
  pgmspace.h - host shim for native unit tests.

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

// Flash strings are plain SRAM on the host: PROGMEM is a no-op, pgm_read_* are
// plain derefs.
//
// This cannot catch PROGMEM misuse. Placement, the strcmp_P-needs-SRAM-arg1
// rule and the don't-subscript-PROGMEM rule are target-only; passing here says
// nothing about them.

#ifndef EG_TEST_PGMSPACE_H
#define EG_TEST_PGMSPACE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

class __FlashStringHelper;

#define PROGMEM
#define PGM_P      const char *
#define PGM_VOID_P const void *
#define PSTR(s)    (s)
#define FPSTR(p)   (reinterpret_cast<const __FlashStringHelper *>(p))
#define F(s)       FPSTR(PSTR(s))

#define pgm_read_byte(a)      (*(const uint8_t *)(a))
#define pgm_read_byte_near(a) pgm_read_byte(a)
#define pgm_read_byte_far(a)  pgm_read_byte(a)
#define pgm_read_word(a)      (*(const uint16_t *)(a))
#define pgm_read_word_near(a) pgm_read_word(a)
#define pgm_read_dword(a)     (*(const uint32_t *)(a))
#define pgm_read_float(a)     (*(const float *)(a))
#define pgm_read_ptr(a)       (*(void * const *)(a))

#define memcpy_P    memcpy
#define memcmp_P    memcmp
#define strcpy_P    strcpy
#define strncpy_P   strncpy
#define strcat_P    strcat
#define strncat_P   strncat
#define strcmp_P    strcmp
#define strncmp_P   strncmp
#define strcasecmp_P strcasecmp
#define strlen_P    strlen
#define strnlen_P   strnlen
#define strstr_P    strstr
#define sprintf_P   sprintf
#define snprintf_P  snprintf
#define vsnprintf_P vsnprintf
#define printf_P    printf

#endif
