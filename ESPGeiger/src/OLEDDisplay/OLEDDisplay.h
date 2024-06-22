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
#include "../Status.h"
#include "../Counter/Counter.h"
#include "logo.h"
#include "fonts.h"

extern Status status;
extern Counter gcounter;

#define OLED_TEXT_BUFFER 1000
#define OLED_TEXT_ROWS   5
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
#define OLED_SDA      5
#endif

#ifndef OLED_SCL
#define OLED_SCL      4
#endif
#ifndef OLED_ADDR
#define OLED_ADDR      0x3c
#endif

class SSD1306Display : public SSD1306Wire{
public:
    SSD1306Display(uint8_t _addr, uint8_t _sda, uint8_t _scl);
  void begin() {
    //Wire.setClock(400000L);
    SSD1306Wire::init();
  #if OLED_FLIP
    flipScreenVertically();
  #endif
    setBrightness(64);
    setFont(ArialMT_Plain_10);
    fontWidth = 8;
    fontHeight = 16;
    clear();
    drawXbm(0, 0, 51, 51, ESPLogo);
    drawString(55, 10, PSTR("ESPGeiger"));
    drawString(55, 24, status.version);
    drawString(55, 42, PSTR("Connecting .."));

    display();	// todo: not very efficient
    setFont(ArialMT_Plain_16);
  }
  void clear() {
    SSD1306Wire::clear();
  }
  void clear(int start, int end) {
    setColor(BLACK);
    fillRect(0, (start+1)*fontHeight, OLED_WIDTH, (end-start+1)*fontHeight);
    setColor(WHITE);
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
  size_t write(uint8_t c) {
    setColor(BLACK);
    fillRect(cx, cy, fontWidth, fontHeight);
    setColor(WHITE);

    //if(c<NUM_CUSTOM_ICONS && custom_chars[c]!=NULL) {
    //	drawXbm(cx, cy, fontWidth, fontHeight, (const byte*) custom_chars[c]);
    //} else {
    drawString(cx, cy, String((char)c));
    //}
    cx += fontWidth;
    display();	// todo: not very efficient
    return 1;
  }
  size_t write(const char* s) {
    uint8_t nc = strlen(s);
    setColor(BLACK);
    fillRect(cx, cy, fontWidth*nc, fontHeight);
    setColor(WHITE);
    drawString(cx, cy, String(s));
    cx += fontWidth*nc;
    display();	// todo: not very efficient
    return nc;
  }

  void setupWifi(const char* s) {
    clear();
    setFont(DialogInput_plain_12);
    drawString(0, 10, PSTR("Setup - Connect to"));
    drawString(0, 24, PSTR("WiFi -"));
    drawString(0, 38, String(s));
    display();
  }

  void loop(unsigned long now) {
    if (now - _last_update >= 500) {
#ifdef GEIGER_PUSHBUTTON
      if ((status.enable_oled_timeout) && (_lcd_timeout > 0) && (now - status.oled_timeout > _lcd_timeout)) {
        if (status.oled_on) {
          displayOff();
          status.oled_on = false;
        }
        return;
      } else {
        if (status.oled_on == false) {
          displayOn();
          status.oled_page=1;
          status.oled_on = true;
          status.oled_last_update = now-20000;
        }
      }
#endif
      if (status.oled_page == 1) {
        if ((now - status.oled_last_update >= 10000) || (status.oled_last_update == 0)) {
          status.oled_last_update = now;
          page_one_clear();
        }
        if ((now % 1000 >= 500) || (status.oled_last_update == now)) {
          page_one_values(now);
        }
        if ((now % 1000 < 500) || (status.oled_last_update == now)) {
          page_one_graph();
        }
      } else if (status.oled_page == 2) {
        if ((now - status.oled_last_update >= 1000) || (status.oled_last_update == 0)) {
          status.oled_last_update = now;
          page_two_full();
        }
      } else if (status.oled_page == 3) {
        if ((now - status.oled_last_update >= 10000) || (status.oled_last_update == 0)) {
          status.oled_last_update = now;
          page_three_full();
        }
      }
      display();
      _last_update = now;
    }
  }

  void page_one_clear();

  void page_one_graph() {
    setColor(BLACK);
    fillRect(0, 35, OLED_WIDTH, 29);
    setColor(WHITE);

    drawLine(0, 63, 90, 63);
    drawLine(90, 35, 90, 63);
    if (gcounter.cpm_history.size() > 0) {
      int histSize = gcounter.cpm_history.size();
      int maxValue = gcounter.cpm_history[0];
      int minValue = histSize > 1 ? gcounter.cpm_history[0]:0;
      for (decltype(gcounter.cpm_history)::index_t i = 0; i < histSize; i++) {
        maxValue = gcounter.cpm_history[i] > maxValue ? gcounter.cpm_history[i] : maxValue;
        minValue = gcounter.cpm_history[i] < minValue ? gcounter.cpm_history[i] : minValue;
      }

      if (minValue > 1) {
        minValue = (int)(minValue * 0.9);
      }
      int xstart = 0;
      if (gcounter.cpm_history.capacity != histSize) {
        xstart = (( 2*( gcounter.cpm_history.capacity-histSize )));
      }

      if (maxValue == 0) {
        setFont(Open_Sans_Regular_Plain_10);
        drawString(93,55, String(minValue));
        return;
      }

      for (decltype(gcounter.cpm_history)::index_t i = 0; i < histSize; i++) {
        int location = ((map((long)gcounter.cpm_history[i], (long)minValue, (long)maxValue, 0, 24 )) * (-1)) + 62;
        drawRect(xstart+i*2, location, 2, (63 - location));
      }
      setFont(Open_Sans_Regular_Plain_10);
      drawString(93,55, String(minValue));
      drawString(93,35, String(maxValue));
    }
  }

  void page_one_values(unsigned long now) {
    setFont(DialogInput_plain_17);
    setColor(BLACK);
    fillRect(45, 0, 72, 32);
    setColor(WHITE);
    drawString(45,0, String(gcounter.get_cpm()).c_str() );
    setFont(DialogInput_plain_12);
    drawString(45,20, String(gcounter.get_usv(),2).c_str() );
    if (gcounter.cpm_history.capacity != gcounter.cpm_history.size()) {
      drawString(98,2, PSTR("W") );
    }
    if (now - status.last_send < 1000) {
      drawXbm(110, 0, fontWidth, fontHeight, _iconimage_remotext);
    }
  }

  void page_two_full();

  void page_three_full() {
    clear();
    setFont(ArialMT_Plain_10);
    char versionString[80] = "";
    sprintf_P (versionString, PSTR("%S / %S"), status.version, status.git_version);
    drawString(0, 5, GEIGER_MODEL);
    drawString(0, 20, versionString);
    drawString(0, 35, PSTR("@steadramon"));
  }

  void setTimeout(int timeout) {
    _lcd_timeout = timeout * 1000;
  }

  private:
    uint8_t cx, cy;
    uint8_t fontWidth, fontHeight;
    unsigned long _last_update = 0;
    unsigned long _last_full = 0;
    unsigned long _lcd_timeout = 300000;
};

#endif
#endif