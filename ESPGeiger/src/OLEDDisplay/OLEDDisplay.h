/*
  OLEDDisplay.h - OLEDDisplay connection class

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

#ifndef OLEDDISP_H
#define OLEDDISP_H
#ifdef SSD1306_DISPLAY
#include <Arduino.h>
#include <Wire.h>
#include "SSD1306Wire.h"
#include "../Util/Globals.h"
#include "../Counter/Counter.h"
#include "../Module/EGModule.h"
#include "../Prefs/EGPrefs.h"

extern Counter gcounter;

#ifndef OLED_PAGES
#define OLED_PAGES 4
#endif

#ifndef OLED_WIDTH
#define OLED_WIDTH       128
#endif
#ifndef OLED_HEIGHT
#define OLED_HEIGHT      64
#endif

#ifndef OLED_FLIP
#define OLED_FLIP      false
#endif

#ifndef OLED_SDA
#define OLED_SDA      4
#endif

#ifndef OLED_SCL
#define OLED_SCL      5
#endif
#ifndef OLED_ADDR
#define OLED_ADDR      0x3c
#endif

// Note: font-using methods live in OLEDDisplay.cpp to keep fonts.h /
// SSD1306Wire's bundled fonts referenced from a single TU. See Fix B
// in the session notes - reduces binary by ~20-30 KB on OLED builds.
class SSD1306Display : public SSD1306Wire, public EGModule {
public:
    SSD1306Display(uint8_t _addr, int _sda, int _scl);
    const char* name() override { return "disp"; }
    uint8_t display_order() override { return 15; }
    uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
    uint16_t warmup_seconds() override { return 0; }
    // setup() is deferred to on_prefs_loaded() so the configured SDA/SCL
    // and other display prefs are honoured on first boot.
    void pre_wifi() override {}
    const EGPrefGroup* prefs_group() override;
    void on_prefs_loaded() override;
#ifndef OLED_PINS_BLOCKED
    void on_prefs_saved() override;
#endif
    const EGLegacyAlias* legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
private:
    uint8_t _pin_sda = OLED_SDA;
    uint8_t _pin_scl = OLED_SCL;
public:

  void setup();
  void setupWifi(const char* s);
  void wifiDisabled();
  void onButtonTap(unsigned long now);
  bool is_present() const { return _present; }

  void clear() {
    SSD1306Wire::clear();
  }

  void clear(int start, int end) {
    setColor(BLACK);
    fillRect(0, (start+1)*fontHeight, OLED_WIDTH, (end-start+1)*fontHeight);
    setColor(WHITE);
  }

  // Body in OLEDDisplay.cpp — keeping it out of the header avoids the
  // cascading recompile that triggered an icache layout shift in 0.10.0.
  void setBrightness(uint8_t brightness);

  bool isScreenOnTime(unsigned long now);

  void loop(unsigned long now) override;
  bool has_loop() override { return true; }
  uint16_t loop_interval_ms() override { return 500; }

  void page_one_clear();
  void page_one_graph();
  void page_one_values(unsigned long now);
  void page_two_full();
  void page_three_full();
  void page_four_static();
  void page_four_matrix();
  void showOTABanner();

  void setTimeout(uint32_t seconds) {
    _lcd_timeout_ms = seconds * 1000;
  }

  uint8_t oled_page = 1;
  unsigned long oled_timeout = 0;
  unsigned long oled_last_update = 0;
  bool oled_on = true;
  bool enable_oled_timeout = true;

  private:
    uint8_t fontWidth, fontHeight;
    bool _present = false;
    uint32_t _lcd_timeout_ms = 0;  // 0 = no idle-off, schedule applies. Pref only loaded when GEIGER_PUSHBUTTON.
    unsigned long _page4_num_last = 0;
    char _page4_num[8] = "";
    uint8_t _page4_variant = 0;
    uint8_t _last_page = 0;        // tracks page change to retune loop interval
};

extern SSD1306Display display;

#endif
#endif
