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
#ifndef NEOPIXEL_h
#define NEOPIXEL_h
#ifdef GEIGER_NEOPIXEL
#include <Arduino.h>
#include <NeoPixelBus.h>
#include "Counter/Counter.h"
#include "Corium/Module/EGModule.h"
#include "Corium/Prefs/EGPrefs.h"

#ifndef NEOPIXEL_PIN
#define NEOPIXEL_PIN 15
#endif

// esp32 uses RMT channel 0
#ifdef ESP32
  typedef NeoPixelBus<NeoGrbFeature, NeoEsp32Rmt0Ws2812xMethod> NeoController;
#else
  typedef NeoPixelBus<NeoGrbFeature, NeoEsp8266BitBangWs2812xMethod> NeoController;
#endif

class NeoPixel : public EGModule {
  public:
    NeoPixel();
    const char* name() override { return PSTR("neopx"); }
    uint8_t display_order() override { return 16; }
    uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
    uint16_t warmup_seconds() override { return 0; }
    void pre_wifi() override { setup(); }
    const EGPrefGroup* prefs_group() override;
    void on_prefs_loaded() override;
    EGLegacyRows legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
    void loop(unsigned long now) override;
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return 30; }
    void setup();
    void blink(uint16_t timer);
    void setBrightness(int input);
    RgbColor color(uint8_t r, uint8_t g, uint8_t b);
    const uint16_t PixelCount = 1;
    int8_t _pin = NEOPIXEL_PIN;   // -1 = none
    uint32_t neoPixelMode = 3;
    uint8_t  _blip_colour_idx = 0;
    uint8_t  _blip_r = 0;
    uint8_t  _blip_g = 0;
    uint8_t  _blip_b = 0;
    uint8_t  _fade_rate = 0;     // 0=off, 1=fast, 2=medium, 3=slow
  protected:
    NeoController* controller_{nullptr};
  private:
    void precompute_blip();
    RgbColor trend_color();
    RgbColor alarm_color(RgbColor rgb);
    unsigned long onTime = 0;
    unsigned long offTime = 0;
    unsigned long blinkInterval = 2000;
    unsigned long nextInterval = 2000;
    unsigned long last_blip = 0;
    int colorSaturation = 15;
    bool _is_off = true;
    bool _swap_rg = false;
};
#endif
#endif
