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
#include "../Voice/Voice.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Prefs/EGPrefs.h"
#include "../Util/Globals.h"
#include <EGHttpServer.h>
#include <math.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "../Voice/Voice.h"

#define _TICK_STR(x) #x
#define TICK_STR(x) _TICK_STR(x)

AudioTick audiotick;
EG_REGISTER_MODULE(audiotick)

static volatile bool s_nm_busy = false;

void AudioTick::setI2SOwned(bool owned) { s_nm_busy = owned; }
bool AudioTick::isI2SOwned() { return s_nm_busy; }

// ---------- Unified audio dispatch ----------

static QueueHandle_t s_audio_q = nullptr;

bool audio_enqueue(const AudioCmd& cmd) {
  if (!s_audio_q) return false;
  return xQueueSend(s_audio_q, &cmd, 0) == pdTRUE;
}

void AudioTick::dispatch(const AudioCmd& cmd) {
  setI2SOwned(true);
  switch (cmd.type) {
    case AC_WORD:     voice.speakWord(cmd.s, 1.0f); break;
    case AC_NUMBER:   voice.speakNumber(cmd.i32); break;
    case AC_KLAXON:   voice.playKlaxon(cmd.i1); break;
    case AC_ANNOUNCE: voice.announce(); break;
    case AC_NUMBERS:  chimeNumbersStation(); break;
    case AC_CHIME:    play_chime_now(cmd.i1); break;
    case AC_CLICK:    playClick(cmd.i1); break;
    default: break;
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
  xTaskCreate(audio_worker, "aud", 4096, nullptr, 1, nullptr);
}

// Morse 0..9. 0=dot, 1=dash.
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


EG_PSTR(TK_L_EN,  "Enable");
EG_PSTR(TK_H_EN,  "Audible Geiger click via I2S amplifier");
EG_PSTR(TK_L_VOL, "Volume");
EG_PSTR(TK_H_VOL, "0-100");
EG_PSTR(TK_L_FREQ,"Tone Hz");
EG_PSTR(TK_H_FREQ,"Click centre frequency (300-4000 Hz)");
EG_PSTR(TK_L_DEC, "Decay ms");
EG_PSTR(TK_H_DEC, "Click decay time constant (2-100 ms)");
EG_PSTR(TK_L_ENG, "Engine");
EG_PSTR(TK_H_ENG, "0=Pool (4 variants), 1=Chirp (single voice)");
EG_PSTR(TK_L_PK,  "Peak %");
EG_PSTR(TK_H_PK,  "Click amplitude ceiling (1-100). Lower = quieter and below NCN trip.");
EG_PSTR(TK_L_CHM, "Boot chime");
EG_PSTR(TK_H_CHM, "0=off, 1=random");
#ifndef AUDIO_TICK_PINS_BLOCKED
EG_PSTR(TK_L_BCLK,"I2S BCLK Pin");
EG_PSTR(TK_L_WS,  "I2S WS Pin");
EG_PSTR(TK_L_DOUT,"I2S DOUT Pin");
EG_PSTR(TK_H_PIN, "Reboot to apply");
#endif

static const EGPref TICK_PREF_ITEMS[] = {
  {"enable", TK_L_EN,   TK_H_EN,   "1",    nullptr, 0,   0,    0, EGP_BOOL, 0},
  {"volume", TK_L_VOL,  TK_H_VOL,  "60",   nullptr, 0,   100,  0, EGP_UINT, 0},
  {"freq",   TK_L_FREQ, TK_H_FREQ, "900",  nullptr, 300, 6000, 0, EGP_UINT, 0},
  {"decay",  TK_L_DEC,  TK_H_DEC,  "4",    nullptr, 2,   100,  0, EGP_UINT, 0},
  {"engine", TK_L_ENG,  TK_H_ENG,  "0",    nullptr, 0,   1,    0, EGP_UINT, 0},
  {"peak",   TK_L_PK,   TK_H_PK,   "85",   nullptr, 1,   100,  0, EGP_UINT, 0},
  {"chime",  TK_L_CHM,  TK_H_CHM,  "1",    nullptr, 0,   8,    0, EGP_UINT, 0},
#ifndef AUDIO_TICK_PINS_BLOCKED
  {"bclk",   TK_L_BCLK, TK_H_PIN,  TICK_STR(AUDIO_TICK_BCLK), nullptr, 0, MAX_GPIO_PIN, 0, EGP_UINT, 0},
  {"ws",     TK_L_WS,   TK_H_PIN,  TICK_STR(AUDIO_TICK_WS),   nullptr, 0, MAX_GPIO_PIN, 0, EGP_UINT, 0},
  {"dout",   TK_L_DOUT, TK_H_PIN,  TICK_STR(AUDIO_TICK_DOUT), nullptr, 0, MAX_GPIO_PIN, 0, EGP_UINT, 0},
#endif
};

static const EGPrefGroup TICK_PREF_GROUP = {
  "tick", "Audio Tick", 1,
  TICK_PREF_ITEMS,
  sizeof(TICK_PREF_ITEMS) / sizeof(TICK_PREF_ITEMS[0]),
  EGP_CAT_OUTPUT,
};

const EGPrefGroup* AudioTick::prefs_group() { return &TICK_PREF_GROUP; }

void AudioTick::on_prefs_loaded() {
  _enabled  = EGPrefs::getBool("tick", "enable");
  _volume   = (uint8_t)EGPrefs::getUInt("tick", "volume");
  _freq_hz  = (uint16_t)EGPrefs::getUInt("tick", "freq");
  _decay_ms = (uint8_t)EGPrefs::getUInt("tick", "decay");
  _engine   = (uint8_t)EGPrefs::getUInt("tick", "engine");
  _peak     = (uint8_t)EGPrefs::getUInt("tick", "peak");
  _chime    = (uint8_t)EGPrefs::getUInt("tick", "chime");
  if (_volume > 100) _volume = 100;
  if (_engine > 1)   _engine = 0;
  if (_peak < 1)     _peak  = 1;
  if (_peak > 100)   _peak  = 100;
  if (_chime  > 8)   _chime  = 1;
#ifndef AUDIO_TICK_PINS_BLOCKED
  _bclk = (uint8_t)EGPrefs::getUInt("tick", "bclk");
  _ws   = (uint8_t)EGPrefs::getUInt("tick", "ws");
  _dout = (uint8_t)EGPrefs::getUInt("tick", "dout");
#endif
}

void AudioTick::begin() {
  if (_i2s_up) return;
  if (!_enabled) return;
  // ESP32 I2S channel naming is inverted from electrical convention:
  // ONLY_LEFT actually drives the slot the NS4168 reads when its CTRL
  // pin is biased to ~2.5V (datasheet "right channel" mode). Switching
  // this to ONLY_RIGHT here results in silence. Confirmed empirically.
  // use_apll=true gives accurate clocking at 22050 Hz.
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = AUDIO_TICK_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    // ALL_LEFT broadcasts mono to both slots so any amp wiring picks it up.
    .channel_format = I2S_CHANNEL_FMT_ALL_LEFT,
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
  Log::console(PSTR("Tick: I2S ready (BCLK=%u WS=%u DOUT=%u)"),
               (unsigned)_bclk, (unsigned)_ws, (unsigned)_dout);
  playBootChime();
}

// Token-bucket throttle. 30 ms refill gives a 33 Hz steady cap; 8-token
// burst absorbs UdpRx gap-fill bunches. Click body is ~6 ms so even a
// max burst stays under the speaker's mechanical recovery time.
void AudioTick::notifyClick(unsigned long now_ms) {
  if (!_enabled || !_i2s_up || _volume == 0) return;
  if (s_nm_busy) return;
  constexpr uint32_t TOKEN_REFILL_MS = 30;
  constexpr uint8_t  TOKEN_MAX       = 8;
  unsigned long elapsed = now_ms - _last_token_ms;
  if (elapsed >= TOKEN_REFILL_MS) {
    uint32_t gained = elapsed / TOKEN_REFILL_MS;
    if (_tokens + gained > TOKEN_MAX) _tokens = TOKEN_MAX;
    else _tokens += (uint8_t)gained;
    _last_token_ms += gained * TOKEN_REFILL_MS;
  }
  if (_tokens == 0) return;
  _tokens--;
  _last_emit_ms = now_ms;
  playClick();
}

void AudioTick::playBootChime() { play_chime_now(_chime); }

void AudioTick::registerRoutes(EGHttpServer& http) {
  // GET /tick            -> plays the configured chime
  // GET /tick?c=N        -> plays chime N (1..8) for preview
  // GET /tick?click=N    -> fires one Geiger click of engine N (0 or 1) for A/B
  http.on("/tick", EGHttpRequest::GET,
    [](EGHttpRequest& req, EGHttpResponse& res, void*) {
      if (!audiotick._i2s_up) { res.send(503, "text/plain", "audio not ready"); return; }
      AudioCmd cmd{};
      const char* k = req.arg("click");
      if (k && k[0]) {
        uint8_t eng = (uint8_t)atoi(k);
        if (eng > 1) { res.send(400, "text/plain", "0..1"); return; }
        cmd.type = AC_CLICK; cmd.i1 = eng;
      } else {
        const char* c = req.arg("c");
        uint8_t which = (c && c[0]) ? (uint8_t)atoi(c) : audiotick._chime;
        if (which > 9) which = 0;
        cmd.type = AC_CHIME; cmd.i1 = which;
      }
      if (audio_enqueue(cmd)) res.send(200, "text/plain", "queued");
      else                    res.send(503, "text/plain", "queue full");
    });
  http.on("/numbers", EGHttpRequest::GET,
    [](EGHttpRequest& req, EGHttpResponse& res, void*) {
      if (!audiotick._i2s_up) { res.send(503, "text/plain", "."); return; }
      constexpr size_t PAD_LEN = 32;
      uint8_t pad[PAD_LEN];
      AudioTick::numbersPad(pad, PAD_LEN);
      char out[PAD_LEN + 1];
      for (size_t i = 0; i < PAD_LEN; i++) out[i] = '0' + pad[i];
      out[PAD_LEN] = '\0';
      AudioCmd cmd{};
      cmd.type = AC_NUMBERS;
      if (!audio_enqueue(cmd)) { res.send(503, "text/plain", "queue full"); return; }
      res.send(200, "text/plain", out);
    });
}

void AudioTick::play_chime_now(uint8_t c) {
  if (!_i2s_up || c == 0) return;
  if (c == 1) {
    uint32_t r = ESP.getCycleCount() ^ (uint32_t)ESP.getEfuseMac();
    r ^= r << 13; r ^= r >> 17; r ^= r << 5;
    c = 2 + (uint8_t)(r % 8);   // 2..9
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
  // 300 ms explicit silence after the chime. Without this the I2S amp's
  // pop-reduction soft-stop kicks in and briefly attenuates the first geiger
  // clicks that arrive after the chime ends.
  constexpr uint16_t SIL_CHUNK = 256;
  int16_t silence[SIL_CHUNK] = {0};
  uint32_t remaining = 300 * AUDIO_TICK_SAMPLE_RATE / 1000;
  while (remaining > 0) {
    const uint16_t n = (remaining > SIL_CHUNK) ? SIL_CHUNK : (uint16_t)remaining;
    size_t written = 0;
    i2s_write(I2S_NUM_0, silence, n * sizeof(int16_t), &written, 200 / portTICK_PERIOD_MS);
    remaining -= n;
  }
}

// Sine or square note with attack/release envelope, followed by gap_ms of
// silence. Streamed to I2S in 256-sample chunks to keep stack use bounded.
void AudioTick::playMelodyNote(uint16_t freq_hz, uint16_t ms,
                               bool square, uint16_t gap_ms) {
  constexpr uint16_t CHUNK = 256;
  const float inv_fs   = 1.0f / (float)AUDIO_TICK_SAMPLE_RATE;
  const float amp      = (_volume / 100.0f) * (square ? 7000.0f : 11000.0f);
  const float w        = TWO_PI * (float)freq_hz * inv_fs;
  const uint32_t total = (uint32_t)ms * AUDIO_TICK_SAMPLE_RATE / 1000;
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
    uint32_t remaining = (uint32_t)gap_ms * AUDIO_TICK_SAMPLE_RATE / 1000;
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
  // Dials 1-9-8-5. Each digit is a sum of two sines at the standard DTMF
  // row/column frequencies.
  static const uint16_t TONES[4][2] = {
    {697, 1209},   // 1
    {852, 1477},   // 9
    {852, 1336},   // 8
    {770, 1336},   // 5
  };
  constexpr uint16_t CHUNK = 256;
  const float inv_fs = 1.0f / (float)AUDIO_TICK_SAMPLE_RATE;
  const float amp    = (_volume / 100.0f) * 14000.0f;
  const uint32_t total  = 100 * AUDIO_TICK_SAMPLE_RATE / 1000;   // 100 ms / digit
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
    int16_t gap[1100] = {0};   // 50 ms gap between digits
    size_t written = 0;
    i2s_write(I2S_NUM_0, gap, sizeof(gap), &written, 80 / portTICK_PERIOD_MS);
  }
}

void AudioTick::chimeBitPing() {
  // Ascending C major triad, square waves.
  playMelodyNote(523,  120, true);   // C5
  playMelodyNote(659,  120, true);   // E5
  playMelodyNote(784,  120, true);   // G5
  playMelodyNote(1047, 200, true);   // C6
}

void AudioTick::chimeMarioCoin() {
  // Short B5 then a held E6, both square waves.
  playMelodyNote(988,  100, true);   // B5
  playMelodyNote(1319, 600, true);   // E6
}

void AudioTick::chimeItemGet() {
  // Four-note ascending arpeggio in D minor with a held final note, square
  // waves.
  playMelodyNote(587,  80, true, 15);    // D5
  playMelodyNote(698,  80, true, 15);    // F5
  playMelodyNote(880,  80, true, 15);    // A5
  playMelodyNote(1175, 300, true, 0);    // D6 (held)
}

// 3-digit Morse callsign, chipid-derived.
void AudioTick::chimeMorseCallsign() {
  constexpr uint16_t U       = 70;
  constexpr uint16_t F       = 700;
  constexpr uint16_t ELE_GAP = U;
  constexpr uint16_t DIG_GAP = U * 3;
  uint64_t mac = ESP.getEfuseMac();
  uint32_t r = (uint32_t)(mac ^ (mac >> 32));
  r ^= r << 13; r ^= r >> 17; r ^= r << 5;
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
  // LCG seeded from chipid: deterministic per device, unique across devices.
  uint64_t h = ESP.getEfuseMac();
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
  voice.silence(800);
  for (uint8_t grp = 0; grp < 2; grp++) {
    for (uint8_t rep = 0; rep < 2; rep++) {
      for (uint8_t d = 0; d < 5; d++) {
        const uint8_t idx = grp * 5 + d;
        const bool last = (d == 4);
        voice.speakDigit(msg[idx], last);
        voice.silence(last ? 600 : 200);
      }
      if (rep == 0) voice.silence(1200);
    }
    if (grp == 0) voice.silence(2200);
  }
  voice.silence(1000);
  chimeFolkMelody();
}

void AudioTick::chimeSputnik() {
  // Pulsed ~1 kHz tone, ~0.3 s on, ~0.3 s off, three repeats.
  for (uint8_t k = 0; k < 3; k++) {
    playMelodyNote(1000, 280);
    // playMelodyNote already adds a ~30 ms gap; pad to ~280 ms.
    int16_t gap[1100] = {0};   // 50 ms each
    size_t written = 0;
    for (uint8_t g = 0; g < 5; g++) {
      i2s_write(I2S_NUM_0, gap, sizeof(gap), &written, 80 / portTICK_PERIOD_MS);
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

// Per-device voice offset: +/-3% on the fundamental, derived from the chip
// MAC so neighbouring units sound subtly different. Computed once, cached.
static float chip_voice_factor() {
  static const float factor = [] {
    uint64_t mac = ESP.getEfuseMac();
    uint8_t mixed = (uint8_t)(mac ^ (mac >> 8) ^ (mac >> 16) ^ (mac >> 24)
                            ^ (mac >> 32) ^ (mac >> 40));
    return 1.0f + ((int32_t)mixed - 128) * (0.03f / 128.0f);
  }();
  return factor;
}

// Chirp 2*f_lo->f_lo, damped sine body, asymmetric kick, DC blocker on out.
void AudioTick::playClickChirp() {
  // +/-3% freq, +/-4% decay jitter per click.
  uint32_t r = ESP.getCycleCount();
  r ^= r << 13; r ^= r >> 17; r ^= r << 5;
  const float j_freq  = 1.0f + (((int32_t)(r        & 0xFF) - 128) * (0.03f/128.0f));
  const float j_decay = 1.0f + (((int32_t)((r >> 8) & 0xFF) - 128) * (0.04f/128.0f));
  uint32_t nr = r ^ 0xCAFEBABE;

  constexpr uint16_t N           = 1024;
  constexpr uint16_t attack_ramp = 7;
  constexpr uint16_t exit_ramp   = 16;
  constexpr float    asym        = 0.8f;
  constexpr float    f2_amp      = 0.35f;
  const float        peak_ceil   = (float)_peak / 100.0f;
  constexpr float    DC_R        = 0.997f;       // 1-pole DC blocker, ~50 Hz
  constexpr float    NOISE_MIX   = 0.10f;
  int16_t buf[N];
  const float fs     = (float)AUDIO_TICK_SAMPLE_RATE;
  const float inv_fs = 1.0f / fs;
  const float amp    = (_volume / 100.0f) * 32000.0f;
  const float tau    = (float)_decay_ms / 1000.0f * j_decay;
  const float f_lo   = (float)_freq_hz * j_freq * chip_voice_factor();
  const float f_hi   = f_lo * 2.0f;
  const float w_f2   = TWO_PI * f_lo * 0.55f * inv_fs;
  // Incremental decay: env *= dec each sample replaces per-sample expf().
  const float env_dec   = expf(-inv_fs / tau);
  const float chirp_dec = expf(-inv_fs / 0.0005f);
  const float asym_dec  = expf(-inv_fs / 0.0006f);
  const float f2_dec    = expf(-inv_fs / 0.004f);
  constexpr uint16_t sustain_n = 130;   // ~6 ms flat at peak before decay
  const uint16_t cutoff = sustain_n + (uint16_t)(6.0f * tau * fs);
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
    nr ^= nr << 13; nr ^= nr >> 17; nr ^= nr << 5;
    const float n = ((int32_t)(nr & 0xFFFF) - 32768) * (1.0f/32768.0f);
    float sample = (sinf(phase) * (1.0f - NOISE_MIX) + n * NOISE_MIX) * eff_env
                   - asym * asym_env;
    f2_phase += w_f2;
    sample += f2_amp * sinf(f2_phase) * f2_env;
    const float y = sample - dc_x + DC_R * dc_y;
    dc_x = sample; dc_y = y;
    int32_t v = (int32_t)(y * amp * peak_ceil);
    if (v >  32767) v =  32767;
    if (v < -32768) v = -32768;
    buf[i] = (int16_t)v;
    if (i >= sustain_n) env *= env_dec;
    chirp_env *= chirp_dec;
    asym_env  *= asym_dec;
    f2_env    *= f2_dec;
  }
  size_t written = 0;
  i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, 10 / portTICK_PERIOD_MS);
}

// Pool: Chirp voice but each click picks one of 4 asym/body slots.
void AudioTick::playClickPool() {
  struct Variant { float asym, f2_amp; };
  static const Variant POOL[4] = {
    { 0.55f, 0.35f },
    { 0.80f, 0.35f },
    { 0.85f, 0.35f },
    { 1.10f, 0.30f },
  };
  uint32_t r = ESP.getCycleCount();
  r ^= r << 13; r ^= r >> 17; r ^= r << 5;
  const uint8_t which   = (r >> 24) & 3;
  const float   j_vol   = 1.0f + (((int32_t)(r        & 0xFF) - 128) * (0.05f/128.0f));
  const float   j_freq  = 1.0f + (((int32_t)((r >> 8) & 0xFF) - 128) * (0.03f/128.0f));
  const float   j_decay = 1.0f + (((int32_t)((r >>16) & 0xFF) - 128) * (0.04f/128.0f));
  uint32_t nr           = r ^ 0xDECADECA;
  const Variant v       = POOL[which];

  constexpr uint16_t N           = 1024;
  constexpr uint16_t attack_ramp = 7;
  constexpr uint16_t exit_ramp   = 16;
  const float        peak_ceil   = (float)_peak / 100.0f;
  constexpr float    DC_R        = 0.997f;
  constexpr float    NOISE_MIX   = 0.10f;
  int16_t buf[N];
  const float fs     = (float)AUDIO_TICK_SAMPLE_RATE;
  const float inv_fs = 1.0f / fs;
  const float amp    = (_volume / 100.0f) * 32000.0f * j_vol;
  const float tau    = (float)_decay_ms / 1000.0f * j_decay;
  const float f_lo   = (float)_freq_hz * chip_voice_factor() * j_freq;
  const float f_hi   = f_lo * 2.0f;
  const float w_f2   = TWO_PI * f_lo * 0.55f * inv_fs;
  const float env_dec   = expf(-inv_fs / tau);
  const float chirp_dec = expf(-inv_fs / 0.0005f);
  const float asym_dec  = expf(-inv_fs / 0.0006f);
  const float f2_dec    = expf(-inv_fs / 0.004f);
  constexpr uint16_t sustain_n = 130;
  const uint16_t cutoff = sustain_n + (uint16_t)(6.0f * tau * fs);
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
    nr ^= nr << 13; nr ^= nr >> 17; nr ^= nr << 5;
    const float n = ((int32_t)(nr & 0xFFFF) - 32768) * (1.0f/32768.0f);
    float sample = (sinf(phase) * (1.0f - NOISE_MIX) + n * NOISE_MIX) * eff_env
                   - v.asym * asym_env;
    f2_phase += w_f2;
    sample += v.f2_amp * sinf(f2_phase) * f2_env;
    const float y = sample - dc_x + DC_R * dc_y;
    dc_x = sample; dc_y = y;
    int32_t vi = (int32_t)(y * amp * peak_ceil);
    if (vi >  32767) vi =  32767;
    if (vi < -32768) vi = -32768;
    buf[i] = (int16_t)vi;
    if (i >= sustain_n) env *= env_dec;
    chirp_env *= chirp_dec;
    asym_env  *= asym_dec;
    f2_env    *= f2_dec;
  }
  size_t written = 0;
  i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, 10 / portTICK_PERIOD_MS);
}

#endif
