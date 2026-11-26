/*
  BarGraph.h - a history as 8 high bar columns.

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

// lo drops to 90% when it's over trim_above, then the range is made at least min_span tall. false if nothing is above 0
inline bool barRange(const int* hist, int n, int trim_above, int min_span, int& lo, int& hi) {
  lo = hi = hist[0];
  for (int i = 1; i < n; i++) {
    if (hist[i] < lo) lo = hist[i];
    if (hist[i] > hi) hi = hist[i];
  }
  bool counts = hi > 0;
  if (lo > trim_above) lo = lo * 9 / 10;
  if (hi - lo < min_span) { lo = hi > min_span ? hi - min_span : 0; hi = lo + min_span; }
  return counts;
}

// oldest first, newest lands in the last column; bit 7 = bottom row
void barColumns(const int* hist, int n, uint8_t* cols, int w, int min_span = 5);

// LEDs lit: green to low, orange to high, red to 2x high
uint8_t zoneLevel(float value, float low, float high,
                  uint8_t green, uint8_t orange, uint8_t red);

// In 1/16 LEDs. Falls 1/12 a frame, climbs at most rise
struct VuNeedle {
  uint16_t level;
  uint16_t shown;
};
// Call once a frame. kick per count, full = top, all in 1/16 LEDs
void vuStep(VuNeedle& v, uint32_t counts, float kick, uint16_t rise, uint16_t full);

#endif
