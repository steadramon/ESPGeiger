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
#include "../ConfigManager/ConfigManager.h"
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
	SSD1306Display(uint8_t _addr, uint8_t _sda, uint8_t _scl) : SSD1306Wire(_addr, _sda, _scl) {
		cx = 0;
		cy = 0;
		//for(byte i=0;i<NUM_CUSTOM_ICONS;i++) custom_chars[i]=NULL;
	}
	void begin() {
        init();
	#if OLED_FLIP
		flipScreenVertically();
	#endif
        setFont(ArialMT_Plain_10);
		fontWidth = 8;
		fontHeight = 16;
        clear();
        drawXbm(0, 0, 51, 51, ESPLogo);
        drawString(55, 10, "ESPGeiger");
        drawString(55, 24, status.version);

		display();	// todo: not very efficient
        setFont(ArialMT_Plain_16);

	}
	void clear() {
		SSD1306Wire::clear();
	}
	void clear(int start, int end) {
		setColor(BLACK);
		fillRect(0, (start+1)*fontHeight, 128, (end-start+1)*fontHeight);
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
		drawString(0, 10, "Setup - Connect to");
		drawString(0, 24, "WiFi -");
		drawString(0, 38, String(s));
        display();
	}

    void loop() {
      unsigned long now = millis();
      if (now - _last_update > 500) {
        //ConfigManager &configManager = ConfigManager::getInstance();
        _last_update = now;
		if ((now - _last_full > 10000) || (_last_full == 0)) {
			_last_full = now;
			clear();
			drawXbm(120, 0, fontWidth, fontHeight, WiFi.status()==WL_CONNECTED?_iconimage_connected:_iconimage_disconnected);
			setFont(DialogInput_plain_12);
			drawString(0,5, "CPM:");
			drawString(0,20, "µSv/h:");
		}

		if ((now % 1000 >= 500) || (_last_full == now)) {
			setFont(DialogInput_plain_17);
			setColor(BLACK);
			fillRect(45, 0, 72, 32);
			setColor(WHITE);
			drawString(45,0, String(gcounter.get_cpm()).c_str() );
	        setFont(DialogInput_plain_12);
			drawString(45,20, String(gcounter.get_usv()).c_str() );
			if (status.cpm_history.capacity != status.cpm_history.size()) {
				drawString(98,2, "W" );
			}
			if (now - status.last_send < 1000) {
				drawXbm(110, 0, fontWidth, fontHeight, _iconimage_remotext);
			}
		}
		if ((now % 1000 < 500) || (_last_full == now)) {
			setColor(BLACK);
			fillRect(0, 35, 128, 29);
			setColor(WHITE);

	        drawLine(0, 63, 90, 63);
	        drawLine(90, 35, 90, 63);

			if (status.cpm_history.size() > 1)  {
				int histSize = status.cpm_history.size();
		        int maxValue = status.cpm_history[0];
				int minValue = histSize > 1 ? status.cpm_history[0]:0;
				for (decltype(status.cpm_history)::index_t i = 0; i < histSize; i++) {
					maxValue = status.cpm_history[i] > maxValue ? status.cpm_history[i] : maxValue;
					minValue = status.cpm_history[i] < minValue ? status.cpm_history[i] : minValue;
				}

				if (minValue > 1) {
					minValue = (int)(minValue * 0.9);
				}
				int xstart = 0;
				if (status.cpm_history.capacity != histSize) {
					xstart = (( 2*( status.cpm_history.capacity-histSize )));
				}

				if (maxValue == 0) {
					setFont(Open_Sans_Regular_Plain_10);
					drawString(93,55, String(minValue));
					return;
				}

				for (decltype(status.cpm_history)::index_t i = 0; i < histSize; i++) {
					int location = ((map((long)status.cpm_history[i], (long)minValue, (long)maxValue, 0, 24 )) * (-1)) + 62;  
					drawRect(xstart+i*2, location, 2, (63 - location));
				}
				setFont(Open_Sans_Regular_Plain_10);
				drawString(93,55, String(minValue));
				drawString(93,35, String(maxValue));
	        } else {
				setFont(DialogInput_plain_12);
				drawString(20,40, "Warmup" );
				setFont(Open_Sans_Regular_Plain_10);
				drawString(93,55, "0");
			}
		}
        display();

      }
    }
private:
	uint8_t cx, cy;
	uint8_t fontWidth, fontHeight;
    unsigned long _last_update = 0;
    unsigned long _last_full = 0;
};

#endif
#endif