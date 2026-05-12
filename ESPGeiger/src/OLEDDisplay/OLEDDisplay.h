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
#include <U8g2lib.h>
#include "../Util/Globals.h"
#include "../Counter/Counter.h"
#include "../Module/EGModule.h"
#include "../Prefs/EGPrefs.h"

class EGHttpServer;

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
#ifndef OLED_TYPE
#define OLED_TYPE      0   // 0=SSD1306, 1=SH1106
#endif

class SSD1306Display : public EGModule {
public:
    enum DisplayType : uint8_t {
      DISP_SSD1306 = 0,
      DISP_SH1106  = 1,
    };

    SSD1306Display();
    const char* name() override { return "disp"; }
    uint8_t display_order() override { return 15; }
    uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
    uint16_t warmup_seconds() override { return 0; }
    void pre_wifi() override {}
    const EGPrefGroup* prefs_group() override;
    void on_prefs_loaded() override;
#ifndef OLED_PINS_BLOCKED
    void on_prefs_saved() override;
#endif
    const EGLegacyAlias* legacy_aliases() override;
    void registerRoutes(EGHttpServer& http) override;

    void setup();
    void setupWifi(const char* s);
    void wifiDisabled();
    void wipeCountdown(int seconds_left);
    void wipeReady();
    void onButtonTap(unsigned long now);
    bool is_present() const { return _present; }

    void setBrightness(uint8_t brightness);

    // u8g2_DrawStr does bare *str++ which crashes on ESP8266 for flash
    // addresses. This wrapper iterates via pgm_read_byte so RAM and PROGMEM
    // strings both work.
    void drawStrP(int16_t x, int16_t y, const char* s);

    bool isScreenOnTime(unsigned long now);

    void loop(unsigned long now) override;
    bool has_loop() override { return true; }
    // 20 ms = 50 Hz. Each loop call either drains one row of a pending send
    // (~1.6 ms peak) or, every 500 ms, renders the next frame. Spreads the
    // sendBuffer blocking time across multiple ticks so no single tick costs
    // the full 6.6-13 ms.
    uint16_t loop_interval_ms() override { return 20; }

    void page_one_clear();
    bool page_one_graph();
    bool page_one_values(unsigned long now);
    void page_two_full();
    void page_three_full();
    void page_four_static();
    void page_four_matrix();
    void showOTABanner();

    void setTimeout(uint32_t seconds) {
      _lcd_timeout_ms = seconds * 1000;
    }

    void clearBuffer()                          { u8g2_ClearBuffer(&_u8g2); }
    void sendBuffer()                           { u8g2_SendBuffer(&_u8g2); }
    void updateDisplayArea(uint8_t tx, uint8_t ty, uint8_t tw, uint8_t th)
                                                { u8g2_UpdateDisplayArea(&_u8g2, tx, ty, tw, th); }
    void setDrawColor(uint8_t c)                { u8g2_SetDrawColor(&_u8g2, c); }
    void setFont(const uint8_t* f)              { u8g2_SetFont(&_u8g2, f); }
    void setFontPosTop()                        { u8g2_SetFontPosTop(&_u8g2); }
    void setContrast(uint8_t c)                 { u8g2_SetContrast(&_u8g2, c); }
    void setFlipMode(uint8_t m)                 { u8g2_SetFlipMode(&_u8g2, m); }
    void setPowerSave(uint8_t s)                { u8g2_SetPowerSave(&_u8g2, s); }
    // No function in the u8g2 C API for these; poke the u8x8 fields directly.
    void setBusClock(uint32_t c)                { _u8g2.u8x8.bus_clock = c; }
    void setI2CAddress(uint8_t a)               { _u8g2.u8x8.i2c_address = a; }
    void drawStr(int16_t x, int16_t y, const char* s)            { u8g2_DrawStr(&_u8g2, x, y, s); }
    void drawBox(int16_t x, int16_t y, int16_t w, int16_t h)     { u8g2_DrawBox(&_u8g2, x, y, w, h); }
    void drawFrame(int16_t x, int16_t y, int16_t w, int16_t h)   { u8g2_DrawFrame(&_u8g2, x, y, w, h); }
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1){ u8g2_DrawLine(&_u8g2, x0, y0, x1, y1); }
    void drawPixel(int16_t x, int16_t y)        { u8g2_DrawPixel(&_u8g2, x, y); }
    void drawXBMP(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bm)
                                                { u8g2_DrawXBMP(&_u8g2, x, y, w, h, bm); }
    uint16_t getStrWidth(const char* s)         { return u8g2_GetStrWidth(&_u8g2, s); }
    uint8_t* getBufferPtr()                     { return u8g2_GetBufferPtr(&_u8g2); }

    uint8_t oled_page = 1;
    unsigned long oled_timeout = 0;
    unsigned long oled_last_update = 0;
    bool oled_on = true;
    bool enable_oled_timeout = true;

  private:
    u8g2_t _u8g2 {};
    uint8_t _pin_sda = OLED_SDA;
    uint8_t _pin_scl = OLED_SCL;
    bool _present = false;
    uint8_t _oled_addr = 0;
    uint8_t _pref_display_type = OLED_TYPE;
    uint32_t _lcd_timeout_ms = 0;   // 0 = use schedule
    unsigned long _page4_num_last = 0;
    char _page4_num[8] = "";
    uint8_t _page4_variant = 0;
    uint8_t _last_page = 0;
    bool _p1_alt = false;
    // Page-1 values cache; 0xFF... sentinels force a render on first call
    // and after page_one_clear.
    uint32_t _p1v_last_cpm = 0xFFFFFFFFu;
    int32_t  _p1v_last_usv_x100 = -1;
    uint8_t  _p1v_last_send_ind = 0;
    bool     _p1v_last_warming = false;
    uint8_t  _p1v_last_wifi = 0xFFu;  // bit0=disabled, bit1=connected
    int8_t   _p1v_last_trend = 0;     // -1=down, 0=flat, +1=up
    // Progressive send queue: drained one tile-row per loop() call.
    // rows_left=0 means idle. OOB FB writers zero rows_left to abandon.
    uint8_t _send_ty = 0;
    uint8_t _send_cur = 0;
    uint8_t _send_rows_left = 0;
    unsigned long _next_render_due = 0;
};

extern SSD1306Display display;

#endif
#endif
