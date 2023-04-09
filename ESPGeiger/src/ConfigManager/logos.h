/*
  logos.h - Images to show on the config webpage
  
  Copyright (C) 2023 @steadramon

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

#include <Arduino.h>

static const char* faviconHead PROGMEM = R"SVG(<link rel="icon" href="data:image/svg+xml;utf8,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 512 512'%3E%3Cpath fill='%23666' d='M256 0a256 256 0 1 1 0 512 256 256 0 0 1 0-512z'/%3E%3Cpath fill='%23FB2' d='M256 36a220 220 0 1 1 0 440 220 220 0 0 1 0-440z'/%3E%3Cpath fill='%23555' d='M256 286a30 30 0 1 0 0-60 30 30 0 0 0 0 60zm28-82 62-109a182 182 0 0 0-182 1l63 109a57 57 0 0 1 57-1zm155 51H313c0 21-11 39-28 49l64 108c54-32 90-90 90-157zM163 412l64-108a57 57 0 0 1-28-49H73c0 67 36 125 90 157z'/%3E%3C/svg%3E">)SVG";
static const char* radSVG PROGMEM = R"SVG(<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 512 512'><path fill='#666' d='M256 0a256 256 0 1 1 0 512 256 256 0 0 1 0-512z'/><path fill='#FB2' d='M256 36a220 220 0 1 1 0 440 220 220 0 0 1 0-440z'/><path fill='#555' d='M256 286a30 30 0 1 0 0-60 30 30 0 0 0 0 60zm28-82 62-109a182 182 0 0 0-182 1l63 109a57 57 0 0 1 57-1zm155 51H313c0 21-11 39-28 49l64 108c54-32 90-90 90-157zM163 412l64-108a57 57 0 0 1-28-49H73c0 67 36 125 90 157z'/></svg>)SVG";