/*
  NeoPixel.cpp - functions to handle NeoPixel
  
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
#ifdef GEIGER_NEOPIXEL

#include <Arduino.h>
#include "NeoPixel.h"
#include "../Logger/Logger.h"

NeoPixel::NeoPixel() {
}

void NeoPixel::setup()
{
#ifdef NEOPIXEL_BITBANG
  this->controller_ = new NeoPixelBus<NeoRgbFeature, NeoEsp8266BitBangWs2812xMethod>(1, NEOPIXEL_PIN);
#else
  this->controller_ = new NeoPixelBus<NeoRgbFeature, NeoEsp8266DmaWs2812xMethod>(1, 1);
#endif
  this->controller_->Begin();
  this->controller_->Show();
}

void NeoPixel::blip()
{
  if (this->neoPixelMode == 0) {
    blink(20);
  }
}

void NeoPixel::blink(uint16 timer)
{
  float our_cpm = gcounter.get_cpmf();
  float our_5cpm = gcounter.get_cpm5f();
  RgbColor rgb(0, colorSaturation, 0);
  if ((our_5cpm > 0) && (our_cpm > 0)) {
    float diff_ratio = (our_cpm / our_5cpm);
    nextInterval = blinkInterval/diff_ratio;
    if (nextInterval < 100) {
      nextInterval = 100;
    }
    if (nextInterval > 4000) {
      nextInterval = 4000;
    }
    if (this->neoPixelMode == 2) {
      if (diff_ratio > 1.5) {
        rgb = RgbColor(colorSaturation, 0, 0);
      } else if (diff_ratio > 1.25) {
        rgb = RgbColor(colorSaturation, colorSaturation, 0);
      } else if (diff_ratio == 1.0) {
        rgb = RgbColor(0, 0, colorSaturation);
      } else if (diff_ratio < 0.50) {
        rgb = RgbColor(colorSaturation, 0, colorSaturation);
      }
    }
  }
  if (this->neoPixelMode != 2) {
    if (gcounter.is_alert()) {
      rgb = RgbColor(colorSaturation, 0, 0);
    } else if (gcounter.is_warning()) {
      rgb = RgbColor(colorSaturation, colorSaturation, 0);
    } else if (our_cpm == 0 && our_5cpm == 0) {
      rgb = RgbColor(0, 0, colorSaturation);
    }
  }

  this->controller_->SetPixelColor(0, rgb);
  this->controller_->Show();
  onTime = millis();
  offTime = timer;
}

void NeoPixel::loop()
{
  unsigned long now = millis();
  if (now - onTime >= offTime)
  {
    RgbColor black(0);
    this->controller_->SetPixelColor(0, black);
    this->controller_->Show();
  }
  if (this->neoPixelMode == 1) {
    if (last_blip != gcounter.last_blip()) {
      last_blip = gcounter.last_blip();
      blink(20);
    }
  } else {
    if (now - onTime >= nextInterval) {
      blink(20);
    }
  }
}
#endif