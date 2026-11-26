/*
  GeigerInput/Type/TestTimerEsp.cpp - test pulse timer, esp_timer on esp32


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
#include "Corium/Platform/Chip.h"

#if GEIGER_IS_TEST(GEIGER_TYPE)

#if !CORIUM_HAS_TIMER1
static uint32_t (*s_fn)();

static esp_timer_handle_t s_hdl = NULL;
static int64_t s_due_us;   // us, timer task only
static volatile bool s_rearm;
static volatile uint32_t s_first;

// Next one is timed from when this was due, so lag doesn't add up
static void IRAM_ATTR on_timer(void*) {
  if (s_rearm) {   // new rate, from loop
    s_rearm = false;
    s_due_us = esp_timer_get_time() + s_first;
    esp_timer_start_once(s_hdl, s_first);
    return;
  }
  uint32_t next = s_fn();
  if (!next) return;
  int64_t now = esp_timer_get_time();
  s_due_us += next;
  if (s_due_us <= now) s_due_us = now + next;   // fell behind, no catch-up burst
  esp_timer_start_once(s_hdl, (uint64_t)(s_due_us - now));
}

void TestTimer::start(uint32_t (*fn)(), uint32_t first) {
  s_fn = fn;
  if (!s_hdl) {
    const esp_timer_create_args_t cfg = { .callback = &on_timer, .name = "testpulse" };
    if (esp_timer_create(&cfg, &s_hdl) != ESP_OK) return;
  }
  esp_timer_stop(s_hdl);   // not running is fine
  s_due_us = esp_timer_get_time() + first;
  esp_timer_start_once(s_hdl, first);
}

void TestTimer::stop() { if (s_hdl) esp_timer_stop(s_hdl); }

// fire now, the timer task re-arms
void TestTimer::restart(uint32_t first) {
  if (!s_hdl) return;
  s_first = first;
  s_rearm = true;
  esp_timer_stop(s_hdl);
  esp_timer_start_once(s_hdl, 1);
}
#endif

#endif
