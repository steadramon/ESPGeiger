/*
  DisplayType.h - Shared display.type index space.

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
#ifndef EG_DISPLAYTYPE_H
#define EG_DISPLAYTYPE_H

// 1 adds the 72x40 panels to the default list
#ifndef OLED_TINY
#ifdef ESP32
#define OLED_TINY 1
#else
#define OLED_TINY 0
#endif
#endif

// display.type, never reorder
// 0 SSD1306, 1 SH1106, 2 SSD1309, 3 Tiny SSD1306, 4 Tiny SH1106, 5..8 led, 9 128x32
#define DISP_LED_FIRST 5

#define DISP_MAX7219_7SEG   (DISP_LED_FIRST + 0)
#define DISP_MAX7219_ROUND  (DISP_LED_FIRST + 1)
#define DISP_MAX7219_MATRIX (DISP_LED_FIRST + 2)
#define DISP_TM1637         (DISP_LED_FIRST + 3)
#define DISP_OLED_128X32    9   // an oled after the led ones

// TM1637 counts too
#define DISP_IS_MAX7219(t)  ((t) >= DISP_LED_FIRST && (t) <= DISP_TM1637)

// display.type/rst/flip defaults
#ifndef OLED_TYPE
#define OLED_TYPE      0   // 0=SSD1306, 1=SH1106, 2=SSD1309
#endif

// types built, bit n = display.type n
#define DISP_LED_TYPES (0xFu << DISP_LED_FIRST)
#ifndef DISPLAY_TYPES
#if defined(OLED_PINS_BLOCKED)
#define DISPLAY_TYPES (1u << OLED_TYPE)
#elif defined(MAX7219_DISPLAY)
#define DISPLAY_TYPES (0x207u | (OLED_TINY ? 0x18u : 0) | DISP_LED_TYPES)   // 0, 1, 2, 9
#else
#define DISPLAY_TYPES (0x207u | (OLED_TINY ? 0x18u : 0))
#endif
#endif
#define DISP_HAS(t)    (((DISPLAY_TYPES) >> (t)) & 1)
#define DISP_MANY      (((DISPLAY_TYPES) & ((DISPLAY_TYPES) - 1)) != 0)   // more than one, so there's a type pref
#define DISP_HAS_TINY  (DISP_HAS(3) || DISP_HAS(4))
#define DISP_HAS_SHORT DISP_HAS(DISP_OLED_128X32)

static_assert(DISP_HAS(OLED_TYPE), "OLED_TYPE not in DISPLAY_TYPES");
// the led ones come with MAX7219_DISPLAY
#if defined(MAX7219_DISPLAY) != ((DISPLAY_TYPES & DISP_LED_TYPES) != 0)
#error "DISPLAY_TYPES led bits and MAX7219_DISPLAY disagree"
#endif

// 255 = none, Heltec and TTGO boards use one
#ifndef OLED_RST
#define OLED_RST      255
#endif

#ifndef OLED_FLIP
#define OLED_FLIP      false
#endif

#endif
