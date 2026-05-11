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
#include "../Module/EGModuleRegistry.h"
#include "../Util/MathUtil.h"

NeoPixel neopixel;
EG_REGISTER_MODULE(neopixel)

EG_PSTR(NP_L_BRT, "Brightness");

static const EGPref NEOPIXEL_PREF_ITEMS[] = {
  {"brightness", NP_L_BRT, nullptr, "15", nullptr, 0, 100, 0, EGP_UINT, EGP_SLIDER},
};

static const EGPrefGroup NEOPIXEL_PREF_GROUP = {
  "neopixel", "NeoPixel", 1,
  NEOPIXEL_PREF_ITEMS,
  sizeof(NEOPIXEL_PREF_ITEMS) / sizeof(NEOPIXEL_PREF_ITEMS[0]),
};

const EGPrefGroup* NeoPixel::prefs_group() { return &NEOPIXEL_PREF_GROUP; }

void NeoPixel::on_prefs_loaded() {
  setBrightness((int)EGPrefs::getUInt("neopixel", "brightness"));
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias NEOPIXEL_LEGACY[] = {
  {"neopixelBrightness", "brightness"},
  {nullptr, nullptr},
};
const EGLegacyAlias* NeoPixel::legacy_aliases() { return NEOPIXEL_LEGACY; }
// === END LEGACY IMPORT ===

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

void NeoPixel::setBrightness(int input) {
  input = clamp(input * 128 / 100, 0, 128);  // 0-100% pref → 0-128 hw, int math
  if (input == 0) {
    RgbColor black(0);
    this->controller_->SetPixelColor(0, black);
    this->controller_->Show();
  }
  colorSaturation = input;
}

void NeoPixel::blip()
{
  if (this->neoPixelMode == 0) {
    blink(20);
  }
}

void NeoPixel::blink(uint16 timer)
{
  if (colorSaturation == 0) {
    return;
  }
  float our_cpm = gcounter.get_cpmf();
  float our_5cpm = gcounter.get_cpm5f();
  RgbColor rgb(0, colorSaturation, 0);
  if ((our_5cpm > 0) && (our_cpm > 0)) {
    float diff_ratio = (our_cpm / our_5cpm);
    nextInterval = clamp((unsigned long)(blinkInterval / diff_ratio), 100UL, 4000UL);
    if (this->neoPixelMode >= 2) {
      // z-score against Poisson std (sqrt of cpm5). Noise-aware: low rates
      // need bigger absolute deltas to trigger, high rates trip on small.
      float z = (our_cpm - our_5cpm) / sqrtf(our_5cpm);
      if (z > 3.0f) {
        rgb = RgbColor(colorSaturation, 0, 0);              // significant rise - red
      } else if (z > 1.5f) {
        rgb = RgbColor(colorSaturation, colorSaturation, 0); // rising - yellow
      } else if (z < -1.5f) {
        rgb = RgbColor(colorSaturation, 0, colorSaturation); // dropping - purple
      } else {
        rgb = RgbColor(0, colorSaturation, 0);              // stable - green
      }
    }
  } else {
    nextInterval = blinkInterval;
    if (this->neoPixelMode >= 2) {
      rgb = RgbColor(0, 0, colorSaturation);              // no data - blue
    }
  }
  if (this->neoPixelMode == 1 || this->neoPixelMode == 3) {
    if (gcounter.is_alert()) {
      rgb = RgbColor(colorSaturation, 0, 0);
    } else if (gcounter.is_warning()) {
      rgb = RgbColor(colorSaturation, colorSaturation, 0);
    } else if (our_cpm < 0.01 && our_5cpm < 0.01) {
      rgb = RgbColor(0, 0, colorSaturation);
    }
  }

  this->controller_->SetPixelColor(0, rgb);
  this->controller_->Show();
  _is_off = false;
  onTime = millis();
  offTime = timer;
}

void NeoPixel::loop(unsigned long now)
{
  if (colorSaturation == 0) {
    return;
  }
  if (!_is_off && now - onTime >= offTime)
  {
    RgbColor black(0);
    this->controller_->SetPixelColor(0, black);
    this->controller_->Show();
    _is_off = true;
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
