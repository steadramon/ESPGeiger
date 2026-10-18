/*
  BarGraph.cpp - CPM history as 8 high bar columns.

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
#include "BarGraph.h"
#include <math.h>

// floor 90% of the low, as the OLED
void barColumns(const int* hist, int n, uint8_t* cols, int w) {
  const int SPAN = 5;
  if (n > w) {
    hist += n - w;
    n = w;
  }
  if (n <= 0) return;
  int lo = hist[0], hi = hist[0];
  for (int i = 1; i < n; i++) {
    if (hist[i] < lo) lo = hist[i];
    if (hist[i] > hi) hi = hist[i];
  }
  if (lo > 1) lo = lo * 9 / 10;
  if (hi - lo < SPAN) { lo = hi > SPAN ? hi - SPAN : 0; hi = lo + SPAN; }
  int span = hi - lo;
  for (int i = 0; i < n; i++) {
    int h = 1 + (hist[i] - lo) * 7 / span;
    cols[w - n + i] = (uint8_t)(0xFF << (8 - h));
  }
}

// any CPM past a threshold lights at least one LED of the next zone
uint8_t zoneLevel(float cpm, float warn, float alert,
                  uint8_t green, uint8_t orange, uint8_t red) {
  if (!(cpm > 0)) return 0;   // NaN too
  if (alert < warn) alert = warn;
  float lo = 0, hi = warn;
  uint8_t base = 0, n = green;
  if (cpm > alert) {
    lo = alert; hi = 2 * alert; base = (uint8_t)(green + orange); n = red;
  } else if (cpm > warn) {
    lo = warn; hi = alert; base = green; n = orange;
  }
  if (hi <= lo) return (uint8_t)(base + n);
  float k = ceilf((cpm - lo) / (hi - lo) * n);
  return (uint8_t)(base + (k > n ? n : k));
}

void vuStep(VuNeedle& v, uint32_t counts, float kick, uint16_t rise, uint16_t full) {
  float up = kick * counts;
  uint32_t level = v.level + (up < full ? (uint32_t)up : full);
  if (level > full) level = full;
  level -= level / 12;
  if (level) level--;
  v.level = (uint16_t)level;
  if (v.shown + rise < level) v.shown = (uint16_t)(v.shown + rise);
  else                        v.shown = v.level;
}
