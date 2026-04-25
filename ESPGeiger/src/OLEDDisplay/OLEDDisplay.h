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
    SSD1306Display(uint8_t _addr, uint8_t _sda, uint8_t _scl);
    const char* name() override { return "disp"; }
    uint8_t display_order() override { return 15; }
    uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
    uint16_t warmup_seconds() override { return 0; }
    void pre_wifi() override { setup(); }
    const EGPrefGroup* prefs_group() override;
    void on_prefs_loaded() override;
    const EGLegacyAlias* legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)

  void setup();
  void setupWifi(const char* s);
  void wifiDisabled();
  void onButtonTap(unsigned long now);

  void clear() {
    SSD1306Wire::clear();
  }

  void clear(int start, int end) {
    setColor(BLACK);
    fillRect(0, (start+1)*fontHeight, OLED_WIDTH, (end-start+1)*fontHeight);
    setColor(WHITE);
  }

  void setBrightness(uint8_t brightness) {
    brightness = (int)brightness * 2.55;
    SSD1306Wire::setBrightness(brightness);
  }

  uint8_t type() { return 1; }
  void noBlink() {/*no support*/}
  void blink() {/*no support*/}
  void setCursor(uint8_t col, int8_t row) {
    /* assume 4 lines, the middle two lines
       are row 0 and 1 */
    cy = (row+1)*fontHeight;
    cx = col*fontWidth;
  }
  void noBacklight() {/*no support*/}
  void backlight() {/*no support*/}
  bool isScreenOnTime(unsigned long now);
  size_t write(uint8_t c) {
    setColor(BLACK);
    fillRect(cx, cy, fontWidth, fontHeight);
    setColor(WHITE);
    char cs[2] = {(char)c, '\0'};
    drawString(cx, cy, cs);
    cx += fontWidth;
    display();
    return 1;
  }
  size_t write(const char* s) {
    if (s == NULL) return 0;
    uint8_t nc = strlen(s);
    setColor(BLACK);
    fillRect(cx, cy, fontWidth*nc, fontHeight);
    setColor(WHITE);
    drawString(cx, cy, s);
    cx += fontWidth*nc;
    display();
    return nc;
  }

  void loop(unsigned long now) override;
  bool has_loop() override { return true; }
  uint16_t loop_interval_ms() override { return 500; }

  void page_one_clear();
  void page_one_graph();
  void page_one_values(unsigned long now);
  void page_two_full();
  void page_three_full();
  void page_four_static();
  void showOTABanner();

  void setTimeout(uint16_t timeout) {
    _lcd_timeout = timeout;
  }

  uint8_t oled_page = 1;
  unsigned long oled_timeout = 0;
  unsigned long oled_last_update = 0;
  bool oled_on = true;
  bool enable_oled_timeout = true;

  private:
    uint8_t cx, cy;
    uint8_t fontWidth, fontHeight;
    uint16_t _lcd_timeout = 300;
    unsigned long _page4_num_last = 0;
    char _page4_num[8] = "";
    uint8_t _last_page = 0;        // tracks page change to retune loop interval
};

extern SSD1306Display display;

#endif
#endif
