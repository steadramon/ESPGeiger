/*
  SerialOut.h - Serial Output class
  
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

#ifndef SERIALOUT_H
#define SERIALOUT_H
#ifdef SERIALOUT
#include <Arduino.h>
#include "../Util/Globals.h"
#include "../Counter/Counter.h"
#include "../Module/EGModule.h"
#include "../GeigerInput/SerialFormat.h"

extern Counter gcounter;

class SerialOut : public EGModule {
    public:
      static constexpr uint8_t SHOW_CPM = 1 << 0;
      static constexpr uint8_t SHOW_USV = 1 << 1;
      static constexpr uint8_t SHOW_HV  = 1 << 2;
      static constexpr uint8_t SHOW_CPS = 1 << 3;

      SerialOut();
      const char* name() override { return "sout"; }
      uint16_t warmup_seconds() override { return 0; }
      bool has_loop() override { return true; }
      uint16_t loop_interval_ms() override { return 500; }
      void loop(unsigned long now) override;
      void registerRoutes(EGHttpServer& http) override;
      const EGPrefGroup* prefs_group() override;
      uint8_t display_order() override { return 15; }
      void on_prefs_loaded() override;
      void set_show(int var);
      void print_cpm();
      void print_usv();
      void toggle_cpm();
      void toggle_usv();
      void toggle_hv();
      void toggle_cps();
      uint16_t interval() const { return _interval; }
      void setInterval(uint16_t v);
      uint8_t format() const { return _format; }
      void setFormat(uint8_t f);
      uint32_t baud() const { return _baud; }
      void setBaud(uint32_t b);
    private:
      void save();
      void applyBaud();
      void resolveFormat();
      unsigned long _last_fire = 0;
      uint8_t _show_flags = 0;
      uint16_t _interval = 0;
      uint8_t _format = 0;     // 0 = labeled (default); 1..4 = SerialFormat protocols
      uint32_t _baud = 0;      // 0 = framework default (115200)
      SerialFormat::FormatFn _fmt_fn = nullptr;  // pre-resolved formatter
};

extern SerialOut serialout;

#endif
#endif
