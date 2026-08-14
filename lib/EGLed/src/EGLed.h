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
#ifndef EGLED_H
#define EGLED_H

#include <Arduino.h>

#ifdef ESP32
// Arduino ledc channels, timer = (ch / 2) % 4. analogWrite takes from the top
#define EGLED_HV_CHANNEL   0
#define EGLED_TONE_CHANNEL 2   // own timer, else tone() retimes hv
#endif

class EGLed {
  public:
    using MillisFn = uint32_t (*)();

    static constexpr uint8_t NO_PIN = 255;   // dark, writes dropped

    EGLed(uint8_t pin, bool low_active = false);
    void attach(uint8_t pin, bool low_active);   // once, for a pin known later

    static void setClock(MillisFn fn);

    void blink(uint16_t on_ms, uint16_t off_ms);
    void pulse(uint16_t on_ms);
    void blinkN(uint16_t on_ms, uint16_t off_ms, uint8_t count);   // 0 stops
    void fade(uint8_t shift);   // bigger shift = slower
    void on();    // steady
    void off();
    void update();   // 1 kHz or every loop

    bool isRunning() const { return _state != IDLE; }

    void setBrightness(uint8_t level) { _brightness = level; }

  private:
    enum : uint8_t { IDLE = 0, PHASE_ON = 1, PHASE_OFF = 2, PHASE_FADE = 3 };

    void writeDuty(uint8_t duty);   // only writer

    uint8_t  _pin;
    uint8_t  _state = IDLE;
    uint8_t  _brightness = 255;
    bool     _low_active;
    uint8_t  _cycles_left = 0;
    uint8_t  _fade_shift = 3;
    uint8_t  _fade_value = 0;
    uint16_t _on_ms  = 0;
    uint16_t _off_ms = 0;
    uint32_t _next_ms = 0;
#ifdef ESP32
    uint8_t  _chan;
#endif
};

#endif
