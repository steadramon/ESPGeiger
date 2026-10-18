/*
  GeigerInputTest.h - Base class for test-type Geiger inputs

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
#ifndef GEIGERINPUTTEST_H
#define GEIGERINPUTTEST_H

#include <Arduino.h>
#include "GeigerInput.h"

#ifndef GEIGER_TESTPULSE_ADJUSTTIME
#define GEIGER_TESTPULSE_ADJUSTTIME 300000
#endif

#ifndef GEIGER_PULSE_WIDTH
#define GEIGER_PULSE_WIDTH 500
#endif

#ifdef ESP8266
// 3.2 us units, EGTimer1 takes them << 8
#define GEIGER_TEST_TIMER_DIV 312500
#define GEIGER_TEST_TICK_SHIFT 8
#else
#define GEIGER_TEST_TIMER_DIV 1000000
#endif

extern volatile int _pulse_width_us;
// isr reads it
extern volatile uint32_t _pulse_width_ticks;
void set_pulse_width_us(int us);

class GeigerInputTest : public GeigerInput
{
  public:
    GeigerInputTest();
    virtual void begin();
    virtual void loop();
    virtual void secondTicker();
    uint32_t boundary_seconds() const override { return 60; }
    void appendClicksExtra(EGHttpResponse& res) override;
    void setTargetCPM(float target, bool manual);
    void setTargetCPS(float target);
    float getTargetCPS();
    void CPMAdjuster();
    double calcDelay();
    char _test_type[16];
    unsigned long _last_pulse_test = 0;
    double _remainder = 0;
    double generatePoissonRand(double lambda);
    // timer side, stamp only, count in loop()
    static void IRAM_ATTR timerPulse();
    // every pass, keep inline
    static void countTimerPulses() { if (_tp_head != _tp_tail) drainTimerPulses(); }
    static void drainTimerPulses();
    int collect() override;
    static volatile uint8_t _tp_head;   // handler
    static volatile uint8_t _tp_tail;   // loop
  protected:
    bool _retarget = false;
  private:
    float _target_cps = 0;
    int _current_selection = -1;
    bool _manual = false;
};
#endif
