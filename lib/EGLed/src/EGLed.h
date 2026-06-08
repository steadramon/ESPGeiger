/*
  EGLed.h - Non-blocking LED driver with on/off state machine over an
  injectable millis source. ESP32 goes direct to LEDC.

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
#ifndef _EGLED_H
#define _EGLED_H

#include <Arduino.h>

class EGLed {
  public:
    using MillisFn = uint32_t (*)();

    EGLed(uint8_t pin, bool low_active = false);

    // Application-wide millis source. Defaults to Arduino's millis().
    static void setClock(MillisFn fn);

    // One on+off cycle: pin active for on_ms, idle for off_ms, then idle.
    void blink(uint16_t on_ms, uint16_t off_ms);
    // Single short pulse: on_ms active, 1 ms idle, then idle.
    void pulse(uint16_t on_ms);
    // count cycles of blink(on_ms, off_ms), then idle. count=0 stops.
    void blinkN(uint16_t on_ms, uint16_t off_ms, uint8_t count);
    // Force idle, cancelling any in-flight effect.
    void off();
    // Advance the state machine. Call from a 1 kHz timer or every loop.
    void update();

    bool isRunning() const { return _state != IDLE; }

    // PWM duty 0-255. 255 = full bright.
    void setBrightness(uint8_t level) { _brightness = level; }

  private:
    enum : uint8_t { IDLE = 0, PHASE_ON = 1, PHASE_OFF = 2 };

    // Write `duty` (0..255) to the output, honouring low_active inversion.
    // Single path for all transitions, so the off side cannot leave a stale
    // PWM channel running.
    void writeDuty(uint8_t duty);

    uint8_t  _pin;
    uint8_t  _state = IDLE;
    uint8_t  _brightness = 255;
    bool     _low_active;
    uint8_t  _cycles_left = 0;
    uint16_t _on_ms  = 0;
    uint16_t _off_ms = 0;
    uint32_t _next_ms = 0;
#ifdef ESP32
    uint8_t  _chan;
#endif
};

#endif
