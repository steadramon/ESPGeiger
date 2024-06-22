/*
  NeoPixel.h - NeoPixel class
  
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
#ifndef _NEOPIXEL_h
#define _NEOPIXEL_h
#ifdef GEIGER_NEOPIXEL
#include <Arduino.h>
#include "../Status.h"
#include <NeoPixelBus.h>
#include "../NTP/NTP.h"
#include "../Counter/Counter.h"

extern Status status;
extern Counter gcounter;

#ifndef NEOPIXEL_MODE
#define NEOPIXEL_MODE 1
#endif

#ifndef NEOPIXEL_PIN
#define NEOPIXEL_PIN 15
#endif

#ifndef NEOPIXEL_BITBANG
#define NEOPIXEL_BITBANG 1
#endif

class NeoPixel {
  public:
    static NeoPixel &getInstance()
    {
      static NeoPixel instance;
      return instance;
    }
    void loop(unsigned long now);
    void setup();
    void blip();
    void blink(uint16 timer);
    void setBrightness(int input);
    const uint16_t PixelCount = 1;
    const uint8_t PixelPin = NEOPIXEL_PIN;
    uint32_t neoPixelMode = 3;
  protected:
    NeoPixelBus<NeoRgbFeature, NeoEsp8266BitBangWs2812xMethod> *controller_{nullptr};
  private:
    NeoPixel();
    unsigned long onTime = 0;
    unsigned long offTime = 0;
    unsigned long blinkInterval = 2000;
    unsigned long nextInterval = 2000;
    unsigned long last_blip = 0;
    int colorSaturation = 15;
};
#endif
#endif