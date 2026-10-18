/*
  BarGraph.h - CPM history as 8 high bar columns.

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
#ifndef EG_BARGRAPH_H
#define EG_BARGRAPH_H
#include <stdint.h>

// oldest first, newest lands in the last column; bit 7 = bottom row
void barColumns(const int* hist, int n, uint8_t* cols, int w);

// LEDs lit: green to warn, orange to alert, red to 2x alert
uint8_t zoneLevel(float cpm, float warn, float alert,
                  uint8_t green, uint8_t orange, uint8_t red);

// 1/16 LEDs, falls 1/12 a frame, climbs at most rise
struct VuNeedle {
  uint16_t level;
  uint16_t shown;
};
void vuStep(VuNeedle& v, uint32_t counts, float kick, uint16_t rise, uint16_t full);

#endif
