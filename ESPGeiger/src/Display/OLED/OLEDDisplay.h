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
#include <stdint.h>
#include "Corium/Platform/Flash.h"
#include <Wire.h>
#include <U8g2lib.h>
#include "App/AppConfig.h"
#include "Corium/Module/Module.h"
#include "Corium/Prefs/Prefs.h"
#include "Display/DisplayBackend.h"
#include "Display/DisplayType.h"

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

#ifndef OLED_SDA
#define OLED_SDA      4
#endif

#ifndef OLED_SCL
#define OLED_SCL      5
#endif

// SSD1306, SH1106 and SSD1309 panels over u8g2, a page per button tap
class SSD1306Display : public corium::Module, public DisplayBackend {
public:
    enum DisplayType : uint8_t {
      DISP_SSD1306        = 0,
      DISP_SH1106         = 1,
      DISP_SSD1309        = 2,
#if OLED_TINY
      DISP_SSD1306_72X40  = 3,
      DISP_SH1106_72X40   = 4,
#endif
    };

#if OLED_TINY
    bool isTiny() const {
      return _type == DISP_SSD1306_72X40
          || _type == DISP_SH1106_72X40;
    }
#else
    bool isTiny() const { return false; }
#endif

    SSD1306Display();
    const char* name() override { return PSTR("oled"); }
    uint8_t priority() override { return CORIUM_PRIORITY_HARDWARE; }

    void start() override;
    void button(unsigned long now, int8_t dir) override { onButtonTap(now, dir); }
    void fx(unsigned long now) override;
    bool screen(const uint8_t*& buf, uint16_t& w, uint16_t& h) override;
    void reload() override;
    bool present() override { return _present; }
    bool takes(uint8_t type) override;
    void unchosen() override { load_pins(); }   // env sensor needs i2c
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
    void wifiDisabled();
    void wipeCountdown(int seconds_left);
    void wipeReady();
    void onButtonTap(unsigned long now, int8_t direction = 1);
    bool is_present() const { return _present; }

    void setBrightness(uint8_t brightness);

    // u8g2_DrawStr crashes on PROGMEM
    void drawStrP(int16_t x, int16_t y, const char* s);

    void loop(unsigned long now) override;

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
    void endOTABanner();

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

    enum : uint8_t { PAGE_MAIN = 1, PAGE_NET = 2, PAGE_ABOUT = 3, PAGE_LAST = PAGE_ABOUT, PAGE_SAVER = 4 };
    uint8_t oled_page = PAGE_MAIN;
    unsigned long oled_last_update = 0;
    bool oled_on = true;
    bool _locked_page = false;
    bool _ota_lock = false;   // OTA banner up

  private:
    enum : uint8_t { DR_NONE = 0, DR_TOP = 1, DR_BOT = 2, DR_FULL = 0xFF };
    void page_changed(unsigned long now);
    void redraw(unsigned long now);   // a frame now, page changed
    void next_frame(unsigned long now);
    void take_events(unsigned long now);
    void draw_frame(unsigned long now);
    uint16_t loop_ms() const;
    unsigned long frame_ms() const;
    bool awake(unsigned long now);
    bool due(unsigned long now, unsigned long every);
    uint8_t render(unsigned long now);
    uint8_t render_main(unsigned long now);
    void render_saver(unsigned long now);
    void render_tiny();
    void queue_rows(uint8_t dirty);
    enum : uint8_t { P1_LIVE, P1_WARMING, P1_SLOW };
    static constexpr uint8_t P1_WIFI_OFF  = 1 << 0;
    static constexpr uint8_t P1_WIFI_UP   = 1 << 1;
    static constexpr uint8_t P1_NO_COUNTS = 1 << 2;
    static constexpr uint8_t P1_WARN      = 1 << 3;
    static constexpr uint8_t P1_ALERT     = 1 << 4;
    struct P1 {
      uint32_t cpm_x10;
      int32_t  usv_x100;
      uint8_t  mode;
      uint8_t  snd;
      uint8_t  flags;
      int8_t   trend;
      bool operator==(const P1& o) const {
        return cpm_x10 == o.cpm_x10 && usv_x100 == o.usv_x100 && mode == o.mode
            && snd == o.snd && flags == o.flags && trend == o.trend;
      }
    };
    void draw_p1_values(const P1& p, float usv);
    void setup_driver();
    void reset_panel();
    void draw_splash();
    void draw_logo(const char* line);   // PROGMEM
    bool take_screen();   // false = no panel
    u8g2_t _u8g2 {};
    uint8_t* _frame = nullptr;
    bool _started = false;
    uint8_t _pin_sda = OLED_SDA;
    uint8_t _pin_scl = OLED_SCL;
    uint8_t _pin_rst = OLED_RST;
    bool _present = false;
    uint8_t _addr = 0;
    uint8_t _type = OLED_TYPE;
    unsigned long _page4_ms = 0;
    char _page4_num[8] = {};
    uint8_t _page4_variant = 0;
    uint8_t _last_page = 0;
    bool _p1_alt = false;
    // 0xFF.. = force a render
    P1 _p1_last = { 0xFFFFFFFFu, -1, P1_LIVE, 0, 0xFFu, 0 };
    uint8_t _sent_mark = 0;   // a send sets 2, draws count down
    uint8_t _send_ty = 0;
    uint8_t _send_cur = 0;
    uint8_t _send_rows_left = 0;      // 0 = idle, direct draws zero it
    uint32_t _next_render_due = 0;
};

extern SSD1306Display oled;

#endif
#endif
