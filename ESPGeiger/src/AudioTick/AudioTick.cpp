/*
  AudioTick.cpp - I2S-driven audible Geiger click.

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
#ifdef AUDIO_TICK

#include "AudioTick.h"
#include "App/AppKeys.h"
#include "App/AppMath.h"
#include "App/ChipVoice.h"
#include "Counter/Counter.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Registry.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/FastMillis.h"
#include "App/AppConfig.h"
#include "Corium/System/PinSafety.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/Util/Debounce.h"
#include "Corium/Util/TokenBucket.h"
#include <EGHttpServer.h>
#include <Ticker.h>
#include <math.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>


CORIUM_PSTR(K_DECAY,  "decay");
CORIUM_PSTR(K_ENGINE, "engine");
CORIUM_PSTR(K_PEAK,   "peak");
CORIUM_PSTR(K_CHIME,  "chime");
CORIUM_PSTR(K_BCLK,   "bclk");
CORIUM_PSTR(K_WS,     "ws");
CORIUM_PSTR(K_DOUT,   "dout");

#define TICK_STR_(x) #x
#define TICK_STR(x) TICK_STR_(x)

AudioTick audiotick;
CORIUM_REGISTER_MODULE(audiotick)

static volatile bool s_nm_busy = false;

void AudioTick::setI2SOwned(bool owned) { s_nm_busy = owned; }

static QueueHandle_t s_audio_q = nullptr;

bool audio_enqueue(const AudioCmd& cmd) {
  if (!s_audio_q) return false;
  return xQueueSend(s_audio_q, &cmd, 0) == pdTRUE;
}

static volatile AudioFn s_speech = nullptr;   // set once, Voice begin

void audio_speech(AudioFn fn) { s_speech = fn; }

static void to_speech(const AudioCmd& cmd) {
  AudioFn fn = s_speech;
  if (fn) fn(cmd);
}

static void speak(uint8_t type, uint8_t i1, int32_t i32) {
  AudioCmd cmd = { type, i1, i32, nullptr };
  to_speech(cmd);
}

void AudioTick::dispatch(const AudioCmd& cmd) {
  if (muted()) return;
  setI2SOwned(true);
  switch (cmd.type) {
    case AC_NUMBERS:  chimeNumbersStation(); break;
    case AC_CHIME:    play_chime_now(cmd.i1); break;
    case AC_CLICK:    playClick(cmd.i1); break;
    default:          to_speech(cmd); break;
  }
  setI2SOwned(false);
}

static void audio_worker(void*) {
  AudioCmd req;
  for (;;) {
    if (xQueueReceive(s_audio_q, &req, portMAX_DELAY) != pdTRUE) continue;
    if (!audiotick.isI2SUp()) continue;
    audiotick.dispatch(req);
  }
}

static void audio_worker_start() {
  if (s_audio_q) return;
  s_audio_q = xQueueCreate(8, sizeof(AudioCmd));
  if (!s_audio_q) {
    Log::console(PSTR("Tick: audio queue alloc failed"));
    return;
  }
  if (xTaskCreate(audio_worker, "aud", 4096, nullptr, 1, nullptr) != pdPASS) {
    Log::console(PSTR("Tick: audio task failed"));
    vQueueDelete(s_audio_q);   // enqueue says no, a later start tries again
    s_audio_q = nullptr;
  }
}

#ifndef AUDIO_MUTE_PIN
#define AUDIO_MUTE_PIN -1
#endif

static Ticker s_mute_ticker;
static Debounce s_mute;
static const uint16_t MUTE_DEBOUNCE_MS = 30;

static void mute_tick() {
  bool down = (digitalRead(AUDIO_MUTE_PIN) == LOW);
  if (!s_mute.step(down, fast_millis(), MUTE_DEBOUNCE_MS)) return;
  if (down) {
    audiotick.toggleMute();
    corium::Registry::post(&audiotick);
  }
}

static void mute_btn_start() {
  if (AUDIO_MUTE_PIN < 0) return;
  if (const char* why = PinSafety::claim_input(AUDIO_MUTE_PIN, PSTR("AudioMute"))) {
    Log::console(PSTR("Tick: mute pin=%d unsafe (%s)"), (int)AUDIO_MUTE_PIN, why);
    return;
  }
  pinMode(AUDIO_MUTE_PIN, INPUT_PULLUP);
  s_mute.reset(digitalRead(AUDIO_MUTE_PIN) == LOW, fast_millis());
  s_mute_ticker.attach_ms(10, mute_tick);
  Log::console(PSTR("Tick: mute btn on GPIO %d"), (int)AUDIO_MUTE_PIN);
}

// 0 = dot, 1 = dash
static const uint8_t MORSE_DIGIT[10][5] = {
  {1,1,1,1,1},  // 0  -----
  {0,1,1,1,1},  // 1  .----
  {0,0,1,1,1},  // 2  ..---
  {0,0,0,1,1},  // 3  ...--
  {0,0,0,0,1},  // 4  ....-
  {0,0,0,0,0},  // 5  .....
  {1,0,0,0,0},  // 6  -....
  {1,1,0,0,0},  // 7  --...
  {1,1,1,0,0},  // 8  ---..
  {1,1,1,1,0},  // 9  ----.
};


CORIUM_PSTR(TK_H_EN,  "Audible Geiger click via I2S amplifier");
CORIUM_PSTR(TK_L_FREQ,"Tone Hz");
CORIUM_PSTR(TK_H_FREQ,"Click centre frequency (300-6000 Hz)");
CORIUM_PSTR(TK_L_DEC, "Decay ms");
CORIUM_PSTR(TK_H_DEC, "Click decay time constant (2-100 ms)");
CORIUM_PSTR(TK_L_ENG, "Engine");
CORIUM_PSTR(TK_O_ENG, "Pool (4 variants)|Chirp (single voice)");
CORIUM_PSTR(TK_L_PK,  "Peak %");
CORIUM_PSTR(TK_H_PK,  "Click amplitude ceiling (1-100). Lower = quieter and below NCN trip.");
CORIUM_PSTR(TK_L_CHM, "Boot chime");
// value = index, as play_chime_now
CORIUM_PSTR(TK_O_CHM, "Off|Random|Three beeps|Folk melody|DTMF|Bit ping|Coin|Sputnik|Item get");
#ifndef AUDIO_TICK_PINS_BLOCKED
CORIUM_PSTR(TK_L_BCLK,"I2S BCLK Pin");
CORIUM_PSTR(TK_L_WS,  "I2S WS Pin");
CORIUM_PSTR(TK_L_DOUT,"I2S DOUT Pin");
#endif

static const corium::Pref TICK_PREF_ITEMS[] PROGMEM = {
  {EG_K_ENABLE, EG_L_EN,   TK_H_EN,   "1",    nullptr, 0,   0,    0, corium::P_BOOL, 0},
  {EG_K_VOLUME, EG_L_VOL,  EG_H_VOL,  "60",   nullptr, 0,   100,  0, corium::P_UINT, 0},
  {EG_K_FREQ,   TK_L_FREQ, TK_H_FREQ, "900",  nullptr, 300, 6000, 0, corium::P_UINT, corium::P_ADVANCED},
  {K_DECAY,     TK_L_DEC,  TK_H_DEC,  "4",    nullptr, 2,   100,  0, corium::P_UINT, corium::P_ADVANCED},
  {K_ENGINE,    TK_L_ENG,  nullptr,   "0",    TK_O_ENG, 0,  1,    0, corium::P_ENUM, corium::P_ADVANCED},
  {K_PEAK,      TK_L_PK,   TK_H_PK,   "85",   nullptr, 1,   100,  0, corium::P_UINT, corium::P_ADVANCED},
  {K_CHIME,     TK_L_CHM,  nullptr,   "1",    TK_O_CHM, 0,  8,    0, corium::P_ENUM, 0},
#ifndef AUDIO_TICK_PINS_BLOCKED
  {K_BCLK,      TK_L_BCLK, corium::H_RBA,  TICK_STR(AUDIO_TICK_BCLK), nullptr, 0, MAX_GPIO_PIN, 0, corium::P_UINT, corium::P_ADVANCED | corium::P_REBOOT | corium::P_PIN_OUT},
  {K_WS,        TK_L_WS,   corium::H_RBA,  TICK_STR(AUDIO_TICK_WS),   nullptr, 0, MAX_GPIO_PIN, 0, corium::P_UINT, corium::P_ADVANCED | corium::P_REBOOT | corium::P_PIN_OUT},
  {K_DOUT,      TK_L_DOUT, corium::H_RBA,  TICK_STR(AUDIO_TICK_DOUT), nullptr, 0, MAX_GPIO_PIN, 0, corium::P_UINT, corium::P_ADVANCED | corium::P_REBOOT | corium::P_PIN_OUT},
#endif
};

static const corium::PrefGroup TICK_PREF_GROUP PROGMEM = {
  "tick", "Audio Tick", 1,
  TICK_PREF_ITEMS,
  sizeof(TICK_PREF_ITEMS) / sizeof(TICK_PREF_ITEMS[0]),
  corium::P_CAT_OUTPUT, EG_K_ENABLE,
};

const corium::PrefGroup* AudioTick::prefs_group() { return &TICK_PREF_GROUP; }

void AudioTick::on_prefs_loaded() {
  _enabled  = corium::Prefs::getBool("tick", EG_K_ENABLE);
  _volume   = (uint8_t)corium::Prefs::getUInt("tick", EG_K_VOLUME);
  _freq_hz  = (uint16_t)corium::Prefs::getUInt("tick", EG_K_FREQ);
  _decay_ms = (uint8_t)corium::Prefs::getUInt("tick", K_DECAY);
  _engine   = (uint8_t)corium::Prefs::getUInt("tick", K_ENGINE);
  _peak     = (uint8_t)corium::Prefs::getUInt("tick", K_PEAK);
  _chime    = (uint8_t)corium::Prefs::getUInt("tick", K_CHIME);
#ifndef AUDIO_TICK_PINS_BLOCKED
  _bclk = (uint8_t)corium::Prefs::getUInt("tick", K_BCLK);
  _ws   = (uint8_t)corium::Prefs::getUInt("tick", K_WS);
  _dout = (uint8_t)corium::Prefs::getUInt("tick", K_DOUT);
#endif
}

static bool claim_i2s_pins(uint8_t bclk, uint8_t ws, uint8_t dout) {
  const uint8_t pins[3] = {bclk, ws, dout};
  for (uint8_t p : pins) {
    if (const char* why = PinSafety::claim_output(p, PSTR("AudioI2S"))) {
      Log::console(PSTR("Tick: I2S pin=%u unsafe (%s)"), (unsigned)p, why);
      return false;
    }
  }
  return true;
}

static void audiotick_click(uint32_t) { audiotick.notifyClick(fast_millis()); }

void AudioTick::begin() {
  gcounter.on_click(audiotick_click);
  gcounter.on_rx_click(audiotick_click);
  if (_i2s_up) return;
  if (!_enabled) return;
  if (!claim_i2s_pins(_bclk, _ws, _dout)) return;
  // ONLY_RIGHT is silent on NS4168
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = AUDIO_TICK_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = AUDIO_TICK_CHANNEL_FMT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = true,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0,
  };
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL) != ESP_OK) {
    Log::console(PSTR("Tick: i2s_driver_install failed"));
    return;
  }
  i2s_pin_config_t pins = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = _bclk,
    .ws_io_num  = _ws,
    .data_out_num = _dout,
    .data_in_num  = I2S_PIN_NO_CHANGE,
  };
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
    Log::console(PSTR("Tick: i2s_set_pin failed"));
    i2s_driver_uninstall(I2S_NUM_0);
    return;
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
  _i2s_up = true;
  audio_worker_start();
  mute_btn_start();
  Log::console(PSTR("Tick: I2S ready (BCLK=%u WS=%u DOUT=%u)"),
               (unsigned)_bclk, (unsigned)_ws, (unsigned)_dout);
  playBootChime();
}

// only posted, by the mute button
void AudioTick::loop(unsigned long) {
  bool m = muted();
  if (m == _muted_logged) return;
  _muted_logged = m;
  Log::console(m ? PSTR("Tick: muted") : PSTR("Tick: unmuted"));
}

// 33 Hz cap, burst of 8
void AudioTick::notifyClick(unsigned long now_ms) {
  if (!_enabled || !_i2s_up || _volume == 0 || muted()) return;
  if (s_nm_busy) return;
  constexpr uint32_t TOKEN_REFILL_MS = 30;
  constexpr uint8_t  TOKEN_MAX       = 8;
  if (!take_token(now_ms, _last_token_ms, _tokens, TOKEN_REFILL_MS, TOKEN_MAX)) return;
  _last_emit_ms = now_ms;
  static const AudioCmd CLICK = { AC_CLICK, 255 };   // pref engine
  audio_enqueue(CLICK);
}

void AudioTick::playBootChime() { play_chime_now(_chime); }

// false = 400 sent
static bool tick_cmd_from(EGHttpRequest& req, EGHttpResponse& res, AudioCmd* cmd,
                          int32_t chime) {
  const char* k = req.arg(F("click"));
  if (k && k[0]) {
    int32_t eng;
    if (!parse_i32(k, &eng) || eng < 0 || eng > 1) { res.send(400, FPSTR(HTTP_CT_PLAIN), F("0..1")); return false; }
    cmd->type = AC_CLICK; cmd->i1 = (uint8_t)eng;
    return true;
  }
  const char* c = req.arg(F("c"));
  int32_t which = chime;
  if (c && c[0] && !parse_i32(c, &which)) which = 0;
  if (which < 0 || which > 9) which = 0;
  cmd->type = AC_CHIME; cmd->i1 = (uint8_t)which;
  return true;
}

void AudioTick::registerRoutes(EGHttpServer& http) {
  http.on(F("/tick"), EGHttpRequest::GET,
    [](EGHttpRequest& req, EGHttpResponse& res, void*) {
      if (!audiotick._i2s_up) { res.send(503, FPSTR(HTTP_CT_PLAIN), F("audio not ready")); return; }
      AudioCmd cmd{};
      if (!tick_cmd_from(req, res, &cmd, audiotick._chime)) return;
      if (audio_enqueue(cmd)) res.send(200, FPSTR(HTTP_CT_PLAIN), F("queued"));
      else                    res.send(503, FPSTR(HTTP_CT_PLAIN), F("queue full"));
    });
  http.on(F("/numbers"), EGHttpRequest::GET,
    [](EGHttpRequest& req, EGHttpResponse& res, void*) {
      if (!audiotick._i2s_up) { res.send(503, FPSTR(HTTP_CT_PLAIN), F(".")); return; }
      constexpr size_t PAD_LEN = 32;
      uint8_t pad[PAD_LEN];
      AudioTick::numbersPad(pad, PAD_LEN);
      char out[PAD_LEN + 1];
      for (size_t i = 0; i < PAD_LEN; i++) out[i] = '0' + pad[i];
      out[PAD_LEN] = '\0';
      AudioCmd cmd{};
      cmd.type = AC_NUMBERS;
      if (!audio_enqueue(cmd)) { res.send(503, FPSTR(HTTP_CT_PLAIN), F("queue full")); return; }
      res.send(200, FPSTR(HTTP_CT_PLAIN), out);
    });
}

// zeros, off the aud task stack
static const int16_t GAP_50MS[1100] = {0};

void AudioTick::play_chime_now(uint8_t c) {
  if (!_i2s_up || c == 0) return;
  if (c == 1) {
    uint32_t r = DeviceInfo::cycles() ^ (uint32_t)DeviceInfo::hwid();
    r = xorshift32(r);
    c = 2 + (uint8_t)(r % 8);
  }
  switch (c) {
    case 2: default: chimeThreeBeeps();    break;
    case 3:          chimeFolkMelody();    break;
    case 4:          chimeDtmf();          break;
    case 5:          chimeBitPing();       break;
    case 6:          chimeMarioCoin();     break;
    case 7:          chimeSputnik();       break;
    case 8:          chimeItemGet();       break;
    case 9:          chimeMorseCallsign(); break;
  }
  // else the amp soft-stop eats the next clicks
  constexpr uint16_t SIL_CHUNK = 256;
  uint32_t remaining = ms_to_samples(300);
  while (remaining > 0) {
    const uint16_t n = (remaining > SIL_CHUNK) ? SIL_CHUNK : (uint16_t)remaining;
    size_t written = 0;
    i2s_write(I2S_NUM_0, GAP_50MS, n * sizeof(int16_t), &written, 200 / portTICK_PERIOD_MS);
    remaining -= n;
  }
}

// 256-sample chunks, keeps stack bounded
void AudioTick::playMelodyNote(uint16_t freq_hz, uint16_t ms,
                               bool square, uint16_t gap_ms) {
  constexpr uint16_t CHUNK = 256;
  const float inv_fs   = 1.0f / (float)AUDIO_TICK_SAMPLE_RATE;
  const float amp      = (_volume / 100.0f) * (square ? 7000.0f : 11000.0f);
  const float w        = TWO_PI * (float)freq_hz * inv_fs;
  const uint32_t total = ms_to_samples(ms);
  const uint32_t attack  = 200;
  const uint32_t release = (total > 1600) ? 800 : total / 4;
  float phase = 0.0f;
  int16_t buf[CHUNK];
  for (uint32_t out = 0; out < total;) {
    const uint16_t n = (total - out > CHUNK) ? CHUNK : (uint16_t)(total - out);
    for (uint16_t i = 0; i < n; i++) {
      const uint32_t pos = out + i;
      float env;
      if (pos < attack)                 env = (float)pos / (float)attack;
      else if (pos > total - release)   env = (float)(total - pos) / (float)release;
      else                              env = 1.0f;
      phase += w;
      float s = sinf(phase);
      if (square) s = (s >= 0.0f) ? 1.0f : -1.0f;
      buf[i] = (int16_t)(s * env * amp);
    }
    size_t written = 0;
    i2s_write(I2S_NUM_0, buf, n * sizeof(int16_t), &written, 50 / portTICK_PERIOD_MS);
    out += n;
  }
  if (gap_ms > 0) {
    for (uint16_t i = 0; i < CHUNK; i++) buf[i] = 0;
    uint32_t remaining = ms_to_samples(gap_ms);
    while (remaining > 0) {
      const uint16_t n = (remaining > CHUNK) ? CHUNK : (uint16_t)remaining;
      size_t written = 0;
      i2s_write(I2S_NUM_0, buf, n * sizeof(int16_t), &written, 200 / portTICK_PERIOD_MS);
      remaining -= n;
    }
  }
}

void AudioTick::chimeThreeBeeps() {
  for (uint8_t k = 0; k < 3; k++) playMelodyNote(900, 50);
}

void AudioTick::chimeFolkMelody() {
  //              freq dur sqr gap
  playMelodyNote(311, 110, true, 50);    // D#4
  playMelodyNote(415, 110, true, 50);    // G#4
  playMelodyNote(415, 110, true, 50);    // G#4
  playMelodyNote(415, 110, true, 50);    // G#4
  playMelodyNote(415, 110, true, 50);    // G#4
  playMelodyNote(392, 110, true, 50);    // G4
  playMelodyNote(349, 110, true, 50);    // F4
  playMelodyNote(311, 320, true, 60);    // D#4 (held)
  playMelodyNote(277, 110, true, 50);    // C#4
  playMelodyNote(262, 110, true, 240);   // C4  (phrase rest)
  playMelodyNote(311, 110, true, 50);    // D#4
  playMelodyNote(415, 110, true, 240);   // G#4 (phrase rest)
  playMelodyNote(415, 110, true, 50);    // G#4
  playMelodyNote(466, 110, true, 240);   // A#4 (phrase rest)
  playMelodyNote(392, 110, true, 50);    // G4
  playMelodyNote(415, 420, true, 0);     // G#4 (final, long held)
}

void AudioTick::chimeDtmf() {
  static const uint16_t TONES[4][2] = {
    {697, 1209},   // 1
    {852, 1477},   // 9
    {852, 1336},   // 8
    {770, 1336},   // 5
  };
  constexpr uint16_t CHUNK = 256;
  const float inv_fs = 1.0f / (float)AUDIO_TICK_SAMPLE_RATE;
  const float amp    = (_volume / 100.0f) * 14000.0f;
  const uint32_t total  = ms_to_samples(100);   // 100 ms / digit
  const uint32_t attack = 200, release = 800;
  int16_t buf[CHUNK];
  for (uint8_t d = 0; d < 4; d++) {
    const float w1 = TWO_PI * (float)TONES[d][0] * inv_fs;
    const float w2 = TWO_PI * (float)TONES[d][1] * inv_fs;
    float p1 = 0.0f, p2 = 0.0f;
    for (uint32_t out = 0; out < total;) {
      const uint16_t n = (total - out > CHUNK) ? CHUNK : (uint16_t)(total - out);
      for (uint16_t i = 0; i < n; i++) {
        const uint32_t pos = out + i;
        float env;
        if (pos < attack)                 env = (float)pos / (float)attack;
        else if (pos > total - release)   env = (float)(total - pos) / (float)release;
        else                              env = 1.0f;
        p1 += w1; p2 += w2;
        buf[i] = (int16_t)((sinf(p1) + sinf(p2)) * env * amp);
      }
      size_t written = 0;
      i2s_write(I2S_NUM_0, buf, n * sizeof(int16_t), &written, 50 / portTICK_PERIOD_MS);
      out += n;
    }
    size_t written = 0;
    i2s_write(I2S_NUM_0, GAP_50MS, sizeof(GAP_50MS), &written, 80 / portTICK_PERIOD_MS);
  }
}

void AudioTick::chimeBitPing() {
  playMelodyNote(523,  120, true);   // C5
  playMelodyNote(659,  120, true);   // E5
  playMelodyNote(784,  120, true);   // G5
  playMelodyNote(1047, 200, true);   // C6
}

void AudioTick::chimeMarioCoin() {
  playMelodyNote(988,  100, true);   // B5
  playMelodyNote(1319, 600, true);   // E6
}

void AudioTick::chimeItemGet() {
  playMelodyNote(587,  80, true, 15);    // D5
  playMelodyNote(698,  80, true, 15);    // F5
  playMelodyNote(880,  80, true, 15);    // A5
  playMelodyNote(1175, 300, true, 0);    // D6 (held)
}

void AudioTick::chimeMorseCallsign() {
  constexpr uint16_t U       = 70;
  constexpr uint16_t F       = 700;
  constexpr uint16_t ELE_GAP = U;
  constexpr uint16_t DIG_GAP = U * 3;
  uint64_t mac = DeviceInfo::hwid();
  uint32_t r = xorshift32(fold32(mac));
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t d = (uint8_t)(r % 10);
    r /= 10;
    if (r == 0) r = (uint32_t)(mac >> (i * 8)) ^ 0x9E3779B1u;
    const uint8_t* code = MORSE_DIGIT[d];
    for (uint8_t e = 0; e < 5; e++) {
      uint16_t gap = (e == 4) ? DIG_GAP : ELE_GAP;
      playMelodyNote(F, code[e] ? U * 3 : U, false, gap);
    }
  }
}

void AudioTick::numbersPad(uint8_t* out, size_t n) {
  uint64_t h = DeviceInfo::hwid();
  for (size_t i = 0; i < n; i++) {
    h = h * 6364136223846793005ULL + 1442695040888963407ULL;
    out[i] = (uint8_t)((h >> 32) % 10);
  }
}

static void numbers_payload(uint8_t out[10]) {
  constexpr uint64_t K = 0xA5A5A5A5A5ULL;
  constexpr uint64_t Q = 0xA684BDACA0ULL;
  const uint64_t P = Q ^ K;
  uint8_t pad[10];
  AudioTick::numbersPad(pad, 10);
  for (int i = 0; i < 10; i++) {
    out[i] = ((uint8_t)((P >> ((9 - i) * 4)) & 0x0F) + pad[i]) % 10;
  }
}

void AudioTick::chimeNumbersStation() {
  uint8_t msg[10];
  numbers_payload(msg);
  chimeFolkMelody();
  speak(AC_SILENCE, 0, 800);
  for (uint8_t grp = 0; grp < 2; grp++) {
    for (uint8_t rep = 0; rep < 2; rep++) {
      for (uint8_t d = 0; d < 5; d++) {
        const uint8_t idx = grp * 5 + d;
        const bool last = (d == 4);
        speak(AC_DIGIT, msg[idx], last);
        speak(AC_SILENCE, 0, last ? 600 : 200);
      }
      if (rep == 0) speak(AC_SILENCE, 0, 1200);
    }
    if (grp == 0) speak(AC_SILENCE, 0, 2200);
  }
  speak(AC_SILENCE, 0, 1000);
  chimeFolkMelody();
}

void AudioTick::chimeSputnik() {
  for (uint8_t k = 0; k < 3; k++) {
    playMelodyNote(1000, 280);
    size_t written = 0;
    for (uint8_t g = 0; g < 5; g++) {
      i2s_write(I2S_NUM_0, GAP_50MS, sizeof(GAP_50MS), &written, 80 / portTICK_PERIOD_MS);
    }
  }
}

void AudioTick::playClick(uint8_t engine_override) {
  const uint8_t e = (engine_override <= 1) ? engine_override : _engine;
  switch (e) {
    case 0: default: playClickPool();  break;
    case 1:          playClickChirp(); break;
  }
}

static float chip_voice_factor() {
  static const float factor = chip_click_pitch(DeviceInfo::hwid());
  return factor;
}

void AudioTick::playClickChirp() {
  uint32_t r = DeviceInfo::cycles();
  r = xorshift32(r);
  const float j_freq = jitter_factor((uint8_t)r, 0.03f);
  const float j_decay = jitter_factor((uint8_t)(r >> 8), 0.04f);
  const float amp  = (_volume / 100.0f) * 32000.0f;
  const float tau  = (float)_decay_ms / 1000.0f * j_decay;
  const float f_lo = (float)_freq_hz * j_freq * chip_voice_factor();
  synthClick(0.8f, 0.35f, amp, tau, f_lo, r ^ 0xCAFEBABE);
}

void AudioTick::playClickPool() {
  struct Variant { float asym, f2_amp; };
  static const Variant POOL[4] = {
    { 0.55f, 0.35f },
    { 0.80f, 0.35f },
    { 0.85f, 0.35f },
    { 1.10f, 0.30f },
  };
  uint32_t r = DeviceInfo::cycles();
  r = xorshift32(r);
  const uint8_t which   = (r >> 24) & 3;
  const float   j_vol   = jitter_factor((uint8_t)r,         0.05f);
  const float   j_freq  = jitter_factor((uint8_t)(r >> 8),  0.03f);
  const float   j_decay = jitter_factor((uint8_t)(r >> 16), 0.04f);
  const Variant v       = POOL[which];
  const float amp  = (_volume / 100.0f) * 32000.0f * j_vol;
  const float tau  = (float)_decay_ms / 1000.0f * j_decay;
  const float f_lo = (float)_freq_hz * chip_voice_factor() * j_freq;
  synthClick(v.asym, v.f2_amp, amp, tau, f_lo, r ^ 0xDECADECA);
}

// f_lo from the caller, the * order differs per engine
void AudioTick::synthClick(float asym, float f2_amp, float amp, float tau, float f_lo,
                           uint32_t nr) {
  constexpr uint16_t N           = 1024;
  constexpr uint16_t attack_ramp = 7;
  constexpr uint16_t exit_ramp   = 16;
  const float        peak_ceil   = (float)_peak / 100.0f;
  constexpr float    DC_R        = 0.997f;       // DC blocker, ~50 Hz
  constexpr float    NOISE_MIX   = 0.10f;
  int16_t buf[N];
  const float fs     = (float)AUDIO_TICK_SAMPLE_RATE;
  const float inv_fs = 1.0f / fs;
  const float f_hi   = f_lo * 2.0f;
  const float w_f2   = TWO_PI * f_lo * 0.55f * inv_fs;
  const float env_dec   = expf(-inv_fs / tau);
  const float chirp_dec = expf(-inv_fs / 0.0005f);
  const float asym_dec  = expf(-inv_fs / 0.0006f);
  const float f2_dec    = expf(-inv_fs / 0.004f);
  constexpr uint16_t sustain_n = 130;   // ~6 ms
  uint16_t cutoff = sustain_n + (uint16_t)(6.0f * tau * fs);
  if (cutoff > N) cutoff = N;   // exit ramp still runs
  float env = 1.0f, chirp_env = 1.0f, asym_env = 1.0f, f2_env = 1.0f;
  float phase = 0.0f, f2_phase = 0.0f;
  float dc_x = 0.0f, dc_y = 0.0f;
  for (uint16_t i = 0; i < N; i++) {
    if (i >= cutoff) { buf[i] = 0; continue; }
    const float f_inst = f_lo + (f_hi - f_lo) * chirp_env;
    phase += TWO_PI * f_inst * inv_fs;
    float eff_env = env;
    if (i < attack_ramp)      eff_env *= (float)i / (float)attack_ramp;
    const uint16_t to_end = cutoff - i;
    if (to_end <= exit_ramp)  eff_env *= (float)to_end / (float)exit_ramp;
    nr = xorshift32(nr);
    const float n = ((int32_t)(nr & 0xFFFF) - 32768) * (1.0f/32768.0f);
    float sample = (sinf(phase) * (1.0f - NOISE_MIX) + n * NOISE_MIX) * eff_env
                   - asym * asym_env;
    f2_phase += w_f2;
    sample += f2_amp * sinf(f2_phase) * f2_env;
    const float y = sample - dc_x + DC_R * dc_y;
    dc_x = sample; dc_y = y;
    buf[i] = sat16((int32_t)(y * amp * peak_ceil));
    if (i >= sustain_n) env *= env_dec;
    chirp_env *= chirp_dec;
    asym_env  *= asym_dec;
    f2_env    *= f2_dec;
  }
  size_t written = 0;
  i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, 10 / portTICK_PERIOD_MS);
}

#endif
