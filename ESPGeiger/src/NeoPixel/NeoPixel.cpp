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
  float our_cpm = status.geigerTicks.get()*60;

  RgbColor rgb(0, colorSaturation, 0);
  if (status.high_cpm_alarm == true) {
    rgb = RgbColor(colorSaturation, 0, 0);
  } else if (status.high_cpm_warning) {
    rgb = RgbColor(colorSaturation, 128, 0);
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
    if (now - onTime >= 2000) {
      blink(20);
    }
  }
}
#endif