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
#include "Corium/System/Globals.h"
#include "Counter/Counter.h"
#include "Corium/Module/EGModule.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Display/DisplayBackend.h"

class EGHttpServer;

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
#define OLED_TYPE      0   // 0=SSD1306, 1=SH1106, 2=SSD1309
#endif

// 255 = none, Heltec and TTGO boards use one
#ifndef OLED_RST
#define OLED_RST      255
#endif

class SSD1306Display : public EGModule, public DisplayBackend {
public:
    enum DisplayType : uint8_t {
      DISP_SSD1306        = 0,
      DISP_SH1106         = 1,
      DISP_SSD1309        = 2,
#ifdef ESP32
      DISP_SSD1306_72X40  = 3,
      DISP_SH1106_72X40   = 4,
#endif
    };

#ifdef ESP32
    bool isTiny() const {
      return _pref_display_type == DISP_SSD1306_72X40
          || _pref_display_type == DISP_SH1106_72X40;
    }
#else
    bool isTiny() const { return false; }
#endif

    SSD1306Display();
    const char* name() override { return PSTR("oled"); }
    uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
    uint16_t warmup_seconds() override { return 0; }

    void start() override;
    void button(unsigned long now, int8_t dir) override { onButtonTap(now, dir); }
    bool screen(const uint8_t*& buf, uint16_t& w, uint16_t& h) override;
    void reload() override;
    bool present() override { return _present; }
    void setup_wifi(const char* host) override { setupWifi(host); }
    void safe_mode() override;
#ifdef GEIGER_PUSHBUTTON
    void offline() override { wifiDisabled(); }
    void wipe(int seconds) override { if (seconds > 0) wipeCountdown(seconds); else wipeReady(); }
    void screen_mode(unsigned long now, bool auto_off) override;
#endif

    void load_pins();   // also the env sensor's i2c pins

    void setup();
    void setupWifi(const char* s);
    void apOnlyMode(const char* host, const char* ip);
    void wifiDisabled();
    void wipeCountdown(int seconds_left);
    void wipeReady();
    void onButtonTap(unsigned long now, int8_t direction = 1);
    bool is_present() const { return _present; }

    void setBrightness(uint8_t brightness);

    // u8g2_DrawStr crashes on PROGMEM
    void drawStrP(int16_t x, int16_t y, const char* s);

    void loop(unsigned long now) override;
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return 20; }

    void page_one_clear();
    bool page_one_graph();
    bool page_one_values(unsigned long now);
    void page_two_full();
    void page_three_full();
    void page_tiny();              // esp32 only
    void page_tiny_dose();
    void page_tiny_net();
    void page_tiny_meter();
    void page_four_static();
    void page_four_matrix();
    void showOTABanner();

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
    // no u8g2 api for these
    void setBusClock(uint32_t c)                { _u8g2.u8x8.bus_clock = c; }
    void setI2CAddress(uint8_t a)               { _u8g2.u8x8.i2c_address = a; }
    void drawStr(int16_t x, int16_t y, const char* s)            { u8g2_DrawStr(&_u8g2, x, y, s); }
    void drawBox(int16_t x, int16_t y, int16_t w, int16_t h)     { u8g2_DrawBox(&_u8g2, x, y, w, h); }
    void drawFrame(int16_t x, int16_t y, int16_t w, int16_t h)   { u8g2_DrawFrame(&_u8g2, x, y, w, h); }
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1){ u8g2_DrawLine(&_u8g2, x0, y0, x1, y1); }
    void drawCircle(int16_t x, int16_t y, int16_t r, uint8_t opt) { u8g2_DrawCircle(&_u8g2, x, y, r, opt); }
    void drawDisc(int16_t x, int16_t y, int16_t r)                { u8g2_DrawDisc(&_u8g2, x, y, r, U8G2_DRAW_ALL); }
    void drawPixel(int16_t x, int16_t y)        { u8g2_DrawPixel(&_u8g2, x, y); }
    void drawXBMP(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bm)
                                                { u8g2_DrawXBMP(&_u8g2, x, y, w, h, bm); }
    uint16_t getStrWidth(const char* s)         { return u8g2_GetStrWidth(&_u8g2, s); }
    uint8_t* getBufferPtr()                     { return u8g2_GetBufferPtr(&_u8g2); }

    enum : uint8_t { PAGE_MAIN = 1, PAGE_LAST = 3, PAGE_SAVER = 4 };
    uint8_t oled_page = PAGE_MAIN;
    unsigned long oled_last_update = 0;
    bool oled_on = true;
    bool _locked_page = false;

  private:
    enum : uint8_t { DR_NONE = 0, DR_TOP = 1, DR_BOT = 2, DR_FULL = 0xFF };
    void page_changed();
    uint16_t tick_ms() const;
    unsigned long frame_ms() const;
    bool awake(unsigned long now);
    bool due(unsigned long now, unsigned long every);
    uint8_t render(unsigned long now);
    uint8_t render_main(unsigned long now);
    void render_saver(unsigned long now);
    void render_tiny();
    void queue_rows(uint8_t dirty);
    void draw_p1_values(uint32_t cpm_x10, float usv, bool warming,
                        uint8_t snd, uint8_t wifi, int8_t trend);
    void setup_driver();
    void reset_panel();
    void draw_splash();
    u8g2_t _u8g2 {};
    uint8_t* _frame = nullptr;
    bool _started = false;
    uint8_t _pin_sda = OLED_SDA;
    uint8_t _pin_scl = OLED_SCL;
    uint8_t _pin_rst = OLED_RST;
    bool _present = false;
    uint8_t _oled_addr = 0;
    uint8_t _pref_display_type = OLED_TYPE;
    unsigned long _page4_num_last = 0;
    char _page4_num[8] = "";
    uint8_t _page4_variant = 0;
    uint8_t _last_page = 0;
    bool _p1_alt = false;
    // 0xFF.. = force a render
    uint32_t _p1v_last_cpm = 0xFFFFFFFFu;
    int32_t  _p1v_last_usv_x100 = -1;
    uint8_t  _p1v_last_send_ind = 0;
    bool     _p1v_last_warming = false;
    uint8_t  _p1v_last_wifi = 0xFFu;  // bits: disabled, connected, no counts
    int8_t   _p1v_last_trend = 0;
    uint8_t _send_ty = 0;
    uint8_t _send_cur = 0;
    uint8_t _send_rows_left = 0;      // 0 = idle, direct draws zero it
    unsigned long _next_render_due = 0;
};

extern SSD1306Display oled;

#endif
#endif
