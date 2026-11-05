/*
  Buzzer.cpp - pulses, bursts and tones on the RMT, idle low.

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
#include "Buzzer.h"
#if BUZZER_BUILT
#include <Arduino.h>
#include <driver/rmt.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_periph.h>
#include <soc/gpio_sig_map.h>
#include "Corium/System/PinSafety.h"

namespace {

constexpr rmt_channel_t CH = RMT_CHANNEL_0;

int s_pin = -1;
corium::Buzzer::Limits s_lim = corium::Buzzer::DEFAULTS;
// elapsed, not due times: micros() wraps
volatile uint32_t s_sent_at = 0;
volatile uint32_t s_busy_us = 0;   // sound length, watch() reads both
uint32_t s_wait_us = 0;            // budget
corium::Buzzer::Run s_run;
volatile bool s_fault = false;
uint8_t s_high = 0;
esp_timer_handle_t s_watch = nullptr;

bool ready(uint32_t now) {
  return s_pin >= 0 && !s_fault && now - s_sent_at >= s_wait_us;
}

void sent(uint32_t now, uint32_t busy, uint32_t wait) {
  s_sent_at = now;
  s_busy_us = busy;
  s_wait_us = wait;
}

void send(const rmt_item32_t* items, int n, uint16_t loops) {
  rmt_set_tx_loop_mode(CH, loops > 0);
  if (loops) {
    rmt_set_tx_loop_count(CH, loops);
    rmt_enable_tx_loop_autostop(CH, true);
  }
  rmt_write_items(CH, items, n, false);
}

rmt_item32_t cycle(uint32_t high, uint32_t low) {
  rmt_item32_t it;
  it.duration0 = high;
  it.level0 = 1;
  it.duration1 = low;
  it.level1 = 0;
  return it;
}

// esp_timer task
void watch(void*) {
  if (micros() - s_sent_at < s_busy_us + 1000 || !gpio_get_level((gpio_num_t)s_pin)) {
    s_high = 0;
    return;
  }
  if (++s_high < corium::Buzzer::GUARD_MS / corium::Buzzer::GUARD_POLL_MS) return;
  rmt_tx_stop(CH);
  esp_rom_gpio_connect_out_signal(s_pin, SIG_GPIO_OUT_IDX, false, false);
  gpio_set_level((gpio_num_t)s_pin, 0);
  s_fault = true;
  esp_timer_stop(s_watch);
}

}

bool corium::Buzzer::begin(int pin, Limits lim) {
  if (corium::PinSafety::claimOutput(pin, PSTR("Buzzer"))) return false;
  rmt_config_t c = RMT_DEFAULT_CONFIG_TX((gpio_num_t)pin, CH);
  c.clk_div = 80;   // 1 us
  c.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  c.tx_config.idle_output_en = true;
  if (rmt_config(&c) != ESP_OK || rmt_driver_install(CH, 0, 0) != ESP_OK) return false;
  const esp_timer_create_args_t w = { .callback = &watch, .name = "buzzer" };
  if (esp_timer_create(&w, &s_watch) != ESP_OK) return false;
  // read the pad back for watch(), routing stays on the RMT
  PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[pin]);
  s_lim = fit(lim);
  s_pin = pin;
  esp_timer_start_periodic(s_watch, GUARD_POLL_MS * 1000);
  return true;
}

bool corium::Buzzer::pulse(uint32_t now, uint16_t us) {
  if (!ready(now)) return false;
  if (us > s_lim.maxHighUs) us = s_lim.maxHighUs;
  if (!us) us = 1;
  rmt_item32_t it = cycle(us, 1);
  send(&it, 1, 0);
  sent(now, us + 1, gapUs(us, s_lim.gapX));
  return true;
}

bool corium::Buzzer::burst(uint32_t now, uint16_t hz, uint8_t cycles) {
  if (!ready(now) || !cycles) return false;
  if (cycles > MAX_BURST) cycles = MAX_BURST;
  hz = clampHz(hz, s_lim);
  uint32_t period = 1000000UL / hz;
  uint32_t high = highUs(hz, capDuty(50, s_lim.dutyPct), s_lim.maxHighUs);
  rmt_item32_t its[MAX_BURST];
  for (uint8_t i = 0; i < cycles; i++) its[i] = cycle(high, period - high);
  send(its, cycles, 0);
  sent(now, cycles * period, gapUs(cycles * high, s_lim.gapX));
  return true;
}

// the player keeps the budget for tones, run cap here
bool corium::Buzzer::tone(uint32_t now, uint16_t hz, uint16_t cycles, uint8_t dutyPct) {
  if (!ready(now) || !cycles) return false;
  if (cycles > MAX_CYCLES) cycles = MAX_CYCLES;
  hz = clampHz(hz, s_lim);
  uint32_t period = 1000000UL / hz;
  cycles = s_run.take(now, s_sent_at + s_busy_us, cycles, period, s_lim);
  if (!cycles) return false;
  uint32_t high = highUs(hz, capDuty(dutyPct, s_lim.dutyPct), s_lim.maxHighUs);
  rmt_item32_t it = cycle(high, period - high);
  send(&it, 1, cycles);
  sent(now, cycles * period, cycles * period);
  return true;
}

uint32_t corium::Buzzer::waitUs(uint32_t now) {
  uint32_t gone = now - s_sent_at;
  return gone >= s_wait_us ? 0 : s_wait_us - gone;
}

bool corium::Buzzer::faulted() { return s_fault; }
#endif
