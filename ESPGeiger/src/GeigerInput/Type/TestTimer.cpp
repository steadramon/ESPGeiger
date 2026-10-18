/*
  GeigerInput/Type/TestTimer.cpp - test pulse timer, EGTimer1 on esp8266, esp_timer on esp32

  Copyright (C) 2026 @steadramon

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
#include "TestTimer.h"
#include "GeigerInput/GeigerInputTest.h"

#if GEIGER_IS_TEST(GEIGER_TYPE)

static uint32_t (*s_fn)();

bool TestTimer::tx_pin(int pin, uint32_t& mask, bool& hi) {
  mask = 0;
  hi = false;
#ifdef ESP8266
  if (pin < 0 || pin > 15) return false;   // 16 is not on GPOS
#elif defined(GPIO_OUT1_W1TS_REG)
  if (pin < 0 || pin > MAX_GPIO_PIN) return false;
#else
  if (pin < 0 || pin > 31) return false;
#endif
  hi = pin >= 32;
  mask = (uint32_t)1 << (pin & 31);
  return true;
}

#ifdef ESP8266
#include <EGTimer1.h>

// EGTimer1 counts cpu ticks
static uint32_t IRAM_ATTR on_nmi() { return s_fn() << GEIGER_TEST_TICK_SHIFT; }

void TestTimer::start(uint32_t (*fn)(), uint32_t first) {
  s_fn = fn;
  EGTimer1::timer_start(on_nmi, first << GEIGER_TEST_TICK_SHIFT);
}

void TestTimer::stop() { EGTimer1::timer_stop(); }

#else
static esp_timer_handle_t s_hdl = NULL;
static int64_t s_due;   // us

// from when it was due, lag doesn't add up
static void IRAM_ATTR on_timer(void*) {
  uint32_t next = s_fn();
  if (!next) return;
  int64_t now = esp_timer_get_time();
  s_due += next;
  if (s_due <= now) s_due = now + next;   // fell behind, no catch-up burst
  esp_timer_start_once(s_hdl, (uint64_t)(s_due - now));
}

void TestTimer::start(uint32_t (*fn)(), uint32_t first) {
  s_fn = fn;
  if (!s_hdl) {
    const esp_timer_create_args_t cfg = { .callback = &on_timer, .name = "testpulse" };
    if (esp_timer_create(&cfg, &s_hdl) != ESP_OK) return;
  }
  esp_timer_stop(s_hdl);   // not running is fine
  s_due = esp_timer_get_time() + first;
  esp_timer_start_once(s_hdl, first);
}

void TestTimer::stop() { if (s_hdl) esp_timer_stop(s_hdl); }
#endif

#endif
