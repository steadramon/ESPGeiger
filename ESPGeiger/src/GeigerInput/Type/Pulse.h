/*
  GeigerInput/Type/Pulse.h - Class for Pulse type counter

  Copyright (C) 2024 @steadramon

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
#ifndef GEIGERINPUTPLS_H
#define GEIGERINPUTPLS_H

#include <stdint.h>
#include "Corium/Platform/Flash.h"
#include "GeigerInput/GeigerInput.h"
#include "Pcnt.h"

#ifndef GEIGER_MODEL
#define GEIGER_MODEL "genpulse"
#endif

// interrupt or PCNT
class GeigerPulse final : public GeigerInput
{
  public:
    GeigerPulse();
    void begin() override;
    void stopForOTA() override;
    void restartAfterOTA() override;
#ifdef COUNT_SELFTEST
    corium::MeasureRows measurements() override;
#endif
#ifndef PCNT_SELECTABLE
    void secondTicker() override { checkIsrStorm(); }
#else
    bool has_pcnt() override { return _pcnt; }
    void use_pcnt(bool on) override { _pcnt = on; }
    void secondTicker() override { if (!_pcnt) checkIsrStorm(); }
    int collect() override;
    void loop() override;
    static constexpr uint32_t DRAIN_MS = 20;   // 50 Hz
    void drain_pcnt();
    void set_pcnt_filter(int val) override;
    void apply_pcnt_filter() override;
    void set_pin_pull(int mode) override;   // 0=floating, 1=up, 2=down
  private:
    bool     _pcnt = true;              // false = interrupt
    uint16_t _pcnt_filter = 0;          // 0-1023 APB cycles, 12.5 ns each
    uint8_t  _pin_pull = PCNT_PIN_PULL_DEFAULT;
    int8_t   _pull_logged = -1;        // -1 = not logged
    int64_t  _last_drain_us = 0;      // esp_timer, no wrap
    uint32_t _pushed = 0;              // into the ring this second
#endif
};
#endif
