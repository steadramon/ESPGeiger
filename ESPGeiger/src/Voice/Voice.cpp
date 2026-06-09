/*
  Voice.cpp - Cori (Piper en_GB) digit playback via IMA ADPCM over I2S.

  Digit WAVs were generated offline with Piper TTS, hand-picked from many
  takes for prosodic consistency, then encoded as 8 kHz / 4-bit IMA ADPCM
  (~2 KB per digit, ~20 KB total). Plus a synthesised klaxon.

  Copyright (C) 2026 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/
#ifdef AUDIO_TICK

#include "Voice.h"
#include "../AudioTick/AudioTick.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Util/Globals.h"
#include <EGHttpServer.h>
#include <driver/i2s.h>

#include "digit_adpcm.h"

Voice voice;
EG_REGISTER_MODULE(voice)

// ---------- IMA ADPCM decoder ----------
// Lo-fi voice playback for the numbers-station easter egg.

static const int8_t IMA_INDEX_TABLE[16] = {
  -1, -1, -1, -1,  2,  4,  6,  8,
  -1, -1, -1, -1,  2,  4,  6,  8,
};

static const int16_t IMA_STEP_TABLE[89] = {
      7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
     19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
     50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
   2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
   5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
  15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static int16_t adpcm_pred = 0;
static int8_t  adpcm_idx  = 0;

static int16_t adpcm_decode_nibble(uint8_t code) {
  int16_t step = IMA_STEP_TABLE[adpcm_idx];
  int32_t diff = step >> 3;
  if (code & 4) diff += step;
  if (code & 2) diff += step >> 1;
  if (code & 1) diff += step >> 2;
  int32_t pred = adpcm_pred;
  if (code & 8) pred -= diff;
  else          pred += diff;
  if (pred >  32767) pred =  32767;
  if (pred < -32768) pred = -32768;
  adpcm_pred = (int16_t)pred;
  int8_t new_idx = adpcm_idx + IMA_INDEX_TABLE[code];
  if (new_idx < 0)  new_idx = 0;
  if (new_idx > 88) new_idx = 88;
  adpcm_idx = new_idx;
  return adpcm_pred;
}

// Phase-accumulator upsampler from 8 kHz ADPCM to 22050 Hz I2S. pitch_scale
// > 1 advances phase faster - shorter playback at higher pitch (which is
// exactly how the rising-fifth on the last digit should sound).
static void adpcm_speak_digit(uint8_t d, float pitch_scale) {
  if (d > 9) return;
  const DigitADPCM& info = DIGIT_ADPCM_TABLE[d];
  if (!info.data) return;

  adpcm_pred = 0;
  adpcm_idx  = 0;

  const float phase_step = (float)DIGIT_ADPCM_RATE * pitch_scale / 22050.0f;
  float phase = 0.0f;

  constexpr int OUT_CHUNK = 256;
  int16_t out[OUT_CHUNK];
  int oc = 0;

  uint8_t pending_byte = pgm_read_byte(&info.data[0]);
  int16_t sample = adpcm_decode_nibble(pending_byte & 0x0F);
  bool need_high = true;
  uint16_t byte_pos = 0;

  uint32_t total_in = info.byte_count * 2;
  uint32_t consumed = 1;
  constexpr int VOICE_SHIFT = 1;   // -6 dB to match chime + click levels
  while (consumed <= total_in) {
    out[oc++] = sample >> VOICE_SHIFT;
    if (oc >= OUT_CHUNK) {
      size_t w;
      i2s_write(I2S_NUM_0, out, OUT_CHUNK * sizeof(int16_t), &w,
                200 / portTICK_PERIOD_MS);
      oc = 0;
    }
    phase += phase_step;
    while (phase >= 1.0f && consumed < total_in) {
      phase -= 1.0f;
      if (need_high) {
        sample = adpcm_decode_nibble((pending_byte >> 4) & 0x0F);
        need_high = false;
      } else {
        byte_pos++;
        if (byte_pos >= info.byte_count) break;
        pending_byte = pgm_read_byte(&info.data[byte_pos]);
        sample = adpcm_decode_nibble(pending_byte & 0x0F);
        need_high = true;
      }
      consumed++;
    }
    if (consumed >= total_in) break;
  }
  if (oc > 0) {
    size_t w;
    i2s_write(I2S_NUM_0, out, oc * sizeof(int16_t), &w,
              200 / portTICK_PERIOD_MS);
  }
  // 20 ms tail fade so the digit-to-silence boundary has no DC step.
  constexpr int FADE_SAMPLES = 22050 * 20 / 1000;
  int16_t fade_buf[FADE_SAMPLES];
  // Tail must match VOICE_SHIFT or the fade itself becomes a click.
  const int16_t tail = adpcm_pred >> VOICE_SHIFT;
  for (int i = 0; i < FADE_SAMPLES; i++) {
    fade_buf[i] = (int16_t)((int32_t)tail * (FADE_SAMPLES - 1 - i) / (FADE_SAMPLES - 1));
  }
  size_t w;
  i2s_write(I2S_NUM_0, fade_buf, FADE_SAMPLES * sizeof(int16_t), &w,
            200 / portTICK_PERIOD_MS);
}

// ---------- Voice public API ----------

void Voice::begin() {
  // Nothing to allocate; ADPCM tables live in PROGMEM.
}

void Voice::silence(uint16_t ms) {
  if (ms == 0) return;
  constexpr uint16_t CHUNK = 256;
  static int16_t zeros[CHUNK] = {0};
  uint32_t remaining = (uint32_t)ms * AUDIO_TICK_SAMPLE_RATE / 1000;
  while (remaining > 0) {
    const uint16_t n = (remaining > CHUNK) ? CHUNK : (uint16_t)remaining;
    size_t w;
    i2s_write(I2S_NUM_0, zeros, n * sizeof(int16_t), &w,
              100 / portTICK_PERIOD_MS);
    remaining -= n;
  }
}

void Voice::speakDigit(uint8_t d, bool rising) {
  if (d > 9) return;
  adpcm_speak_digit(d, rising ? 1.1f : 1.0f);   // rising-fifth on last digit
}

void Voice::say(const char* text) {
  // ADPCM playback can only speak the 10 pre-recorded digits. Walk the
  // string and speak any digit characters; ignore other text.
  speakDigits(text);
}

void Voice::speakDigits(const char* s) {
  if (!s) return;
  const size_t len = strlen(s);
  for (size_t i = 0; i < len; i++) {
    const char c = s[i];
    if (c >= '0' && c <= '9') {
      const bool rising = (i + 1 == len) || (s[i + 1] == ' ');
      speakDigit((uint8_t)(c - '0'), rising);
      silence(200);
    } else if (c == ' ' || c == '-') {
      silence(400);
    }
  }
}

void Voice::registerRoutes(EGHttpServer& http) {
  // GET /adpcm?say=<digits> - Lincolnshire-Poacher style digit playback.
  // GET /adpcm?d=N&scale=F  - single digit at optional pitch scale.
  http.on("/adpcm", EGHttpRequest::GET,
    [](EGHttpRequest& req, EGHttpResponse& res, void*) {
      struct I2SLock {
        I2SLock()  { AudioTick::setI2SOwned(true); }
        ~I2SLock() { AudioTick::setI2SOwned(false); }
      } _lock;
      const char* sayArg = req.arg("say");
      if (sayArg && sayArg[0]) {
        voice.speakDigits(sayArg);
        res.send(200, "text/plain", sayArg);
        return;
      }
      const char* dArg = req.arg("d");
      if (dArg && dArg[0]) {
        uint8_t d = (uint8_t)atoi(dArg);
        if (d > 9) { res.send(400, "text/plain", "0-9"); return; }
        const char* scaleArg = req.arg("scale");
        float scale = (scaleArg && scaleArg[0]) ? atof(scaleArg) : 1.0f;
        adpcm_speak_digit(d, scale);
        res.send(200, "text/plain", dArg);
        return;
      }
      res.send(400, "text/plain", "say= or d= required");
    });

  // GET /klaxon - civil-defense style wailing siren (square-wave fallback;
  // a recorded version belongs in digit_adpcm.h as a future addition).
  http.on("/klaxon", EGHttpRequest::GET,
    [](EGHttpRequest& req, EGHttpResponse& res, void*) {
      struct I2SLock {
        I2SLock()  { AudioTick::setI2SOwned(true); }
        ~I2SLock() { AudioTick::setI2SOwned(false); }
      } _lock;
      const uint16_t tones[] = { 880, 660, 880 };
      constexpr int TONE_MS  = 200;
      constexpr int N_CYCLES = 3;
      constexpr int OUT_CHUNK = 256;
      int16_t out[OUT_CHUNK];
      for (int cycle = 0; cycle < N_CYCLES; cycle++) {
        for (uint16_t f : tones) {
          const uint32_t half_period = (22050 / f) / 2;
          uint32_t phase = 0;
          int oc = 0;
          uint32_t total = (uint32_t)TONE_MS * 22050 / 1000;
          for (uint32_t i = 0; i < total; i++) {
            int env = 32767;
            const int ramp = 220;
            if (i < (uint32_t)ramp) env = (int)i * 32767 / ramp;
            else if (i > total - ramp) env = (int)(total - i) * 32767 / ramp;
            int16_t s = (phase < half_period) ? env : -env;
            out[oc++] = s;
            if (oc >= OUT_CHUNK) {
              size_t w;
              i2s_write(I2S_NUM_0, out, OUT_CHUNK * sizeof(int16_t), &w,
                        200 / portTICK_PERIOD_MS);
              oc = 0;
            }
            phase++;
            if (phase >= half_period * 2) phase = 0;
          }
          if (oc > 0) {
            size_t w;
            i2s_write(I2S_NUM_0, out, oc * sizeof(int16_t), &w,
                      200 / portTICK_PERIOD_MS);
          }
        }
      }
      res.send(200, "text/plain", "klaxon");
    });
}

#endif
