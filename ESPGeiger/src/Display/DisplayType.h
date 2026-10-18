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

// display.type, never reorder
// 3 and 4 (tiny) are esp32 only
#ifdef ESP32
  #define DISP_OLED_COUNT 5   // SSD1306, SH1106, SSD1309, Tiny SSD1306, Tiny SH1106
#else
  #define DISP_OLED_COUNT 3   // SSD1306, SH1106, SSD1309
#endif
#define DISP_LED_FIRST 5

#define DISP_MAX7219_7SEG   (DISP_LED_FIRST + 0)
#define DISP_MAX7219_ROUND  (DISP_LED_FIRST + 1)
#define DISP_MAX7219_MATRIX (DISP_LED_FIRST + 2)
#define DISP_TM1637         (DISP_LED_FIRST + 3)

// TM1637 too
#define DISP_IS_MAX7219(t)  ((t) >= DISP_LED_FIRST)

#endif
