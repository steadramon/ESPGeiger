/*
  DisplayBackend.h - What the Display layer asks of the panel it chose.

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
#ifndef EG_DISPLAYBACKEND_H
#define EG_DISPLAYBACKEND_H
#include <stdint.h>

class DisplayBackend {
  public:
    virtual void start() = 0;
    virtual void button(unsigned long now, int8_t dir) = 0;
    // page-major, h 0 = round, false = none
    virtual bool screen(const uint8_t*& buf, uint16_t& w, uint16_t& h) = 0;
    virtual void reload() {}
    virtual bool present() { return true; }

    virtual void setup_wifi(const char* host) { (void)host; }
    virtual void safe_mode() {}   // starts the panel too
#ifdef GEIGER_PUSHBUTTON
    virtual void offline() {}
    virtual void wipe(int seconds) { (void)seconds; }   // 0 = release to wipe
    virtual void screen_mode(unsigned long now, bool auto_off) { (void)now; (void)auto_off; }
#endif
};

#endif
