/*
  FeatureBits.h - Feature bits for WebAPI "fl" and /info.

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
#ifndef APP_FEATURE_BITS_H
#define APP_FEATURE_BITS_H

// EG_REGISTER_FEATURE, add bits only, never renumber
#define FEAT_MQTT       0
#define FEAT_RADMON     1
#define FEAT_THINGSPEAK 2
#define FEAT_WEBHOOK    3
#define FEAT_SERIALOUT  4
#define FEAT_GMC        5
#define FEAT_OLED       6
#define FEAT_BUTTON     7
#define FEAT_SDCARD     8
#define FEAT_NEOPIXEL   9
#define FEAT_HV         10

#endif
