/*
  Voice.cpp - Cori (Piper en_GB) digit playback via IMA ADPCM over I2S.

  Digit WAVs were generated offline with Piper TTS, hand-picked from many
  takes for prosodic consistency, then encoded as 8 kHz / 4-bit IMA ADPCM
  (~2 KB per digit, ~20 KB total) via tools/piper/adpcm_encode.py. The
  encoder uses ffmpeg+SoXR for resampling and audioop for ADPCM. Plus a
  synthesised klaxon.

  Copyright (C) 2026 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/
#ifdef AUDIO_TICK

#include "Voice.h"
#include "../AudioTick/AudioTick.h"
#include "../Counter/Counter.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Prefs/EGPrefs.h"
#include "../Util/FastMillis.h"
#include "../Util/Globals.h"
#include "../Util/StringUtil.h"
#include "../NTP/NTP.h"
#include <EGHttpServer.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "vocab_adpcm.h"
#include "../AudioTick/AudioTick.h"  // atmosphere_sample mixer

extern Counter gcounter;

Voice voice;
EG_REGISTER_MODULE(voice)

// ---------- Voice prefs (speech) ----------

EG_PSTR(VX_L_VVOL,"Volume");
EG_PSTR(VX_H_VVOL,"0-100");
EG_PSTR(VX_L_AEN, "Announce");
EG_PSTR(VX_H_AEN, "Periodically speak the current CPM");
EG_PSTR(VX_L_AIV, "Every (seconds)");
EG_PSTR(VX_H_AIV, "10-3600");
EG_PSTR(VX_L_AUS, "Announce dose");
EG_PSTR(VX_H_AUS, "Include micro sievert per hour with the announcement");
EG_PSTR(VX_L_ARM, "RadMon up/down");
EG_PSTR(VX_H_ARM, "Speak when RadMon goes online or offline");

static const EGPref VOICE_PREF_ITEMS[] = {
  {"announce_enable",   VX_L_AEN,  VX_H_AEN,  "0",  nullptr,  0,    0,  0, EGP_BOOL, 0},
  {"announce_interval", VX_L_AIV,  VX_H_AIV,  "60", nullptr, 10, 3600,  0, EGP_UINT, EGP_ADVANCED},
  {"announce_usv",      VX_L_AUS,  VX_H_AUS,  "0",  nullptr,  0,    0,  0, EGP_BOOL, 0},
  {"announce_radmon",   VX_L_ARM,  VX_H_ARM,  "0",  nullptr,  0,    0,  0, EGP_BOOL, 0},
  {"voice_volume",      VX_L_VVOL, VX_H_VVOL, "80", nullptr,  1,  100,  0, EGP_UINT, 0},
};

static const EGPrefGroup VOICE_PREF_GROUP = {
  "klaxon", "Voice", 1,
  VOICE_PREF_ITEMS,
  sizeof(VOICE_PREF_ITEMS) / sizeof(VOICE_PREF_ITEMS[0]),
  EGP_CAT_OUTPUT, "announce_enable",
};

const EGPrefGroup* Voice::prefs_group() { return &VOICE_PREF_GROUP; }

void Voice::on_prefs_loaded() {
  _voice_vol = (uint8_t)EGPrefs::getUInt("klaxon", "voice_volume");
  if (_voice_vol < 1)   _voice_vol = 1;
  if (_voice_vol > 100) _voice_vol = 100;
  _ann_en     = EGPrefs::getBool("klaxon", "announce_enable");
  _ann_iv     = (uint16_t)EGPrefs::getUInt("klaxon", "announce_interval");
  _ann_usv    = EGPrefs::getBool("klaxon", "announce_usv");
  _ann_radmon = EGPrefs::getBool("klaxon", "announce_radmon");
  if (_ann_iv < 10)   _ann_iv = 10;
  if (_ann_iv > 3600) _ann_iv = 3600;
  _klax_en   = EGPrefs::getBool("klax", "enable");
  _klax_type = (uint8_t)EGPrefs::getUInt("klax", "type");
  _klax_vol  = (uint8_t)EGPrefs::getUInt("klax", "volume");
  _klax_cd   = (uint8_t)EGPrefs::getUInt("klax", "cooldown");
  if (_klax_type > 4)   _klax_type = 0;
  if (_klax_vol  < 1)   _klax_vol  = 1;
  if (_klax_vol  > 100) _klax_vol  = 100;
  if (_klax_cd   < 1)   _klax_cd   = 1;
  if (_klax_cd   > 60)  _klax_cd   = 60;
  ParsedTime qf = parseTime(EGPrefs::getString("klax", "quiet_from"));
  ParsedTime qt = parseTime(EGPrefs::getString("klax", "quiet_to"));
  _aq_from_min = qf.isValid ? (int16_t)(qf.hour * 60 + qf.minute) : -1;
  _aq_to_min   = qt.isValid ? (int16_t)(qt.hour * 60 + qt.minute) : -1;
}

bool Voice::audioQuietNow() {
  if (_aq_from_min < 0 || _aq_to_min < 0) return false;
  if (_aq_from_min == _aq_to_min)         return false;
  // Cache to the minute boundary so localTm() runs once per minute, not per call.
  static unsigned long s_recompute_ms = 0;
  static bool s_cached = false;
  unsigned long now_ms = fast_millis();
  if ((long)(now_ms - s_recompute_ms) < 0) return s_cached;
  if (!ntpclient.synced)                  return false;
  struct tm t;
  if (!ntpclient.localTm(&t))             return false;
  int16_t m = t.tm_hour * 60 + t.tm_min;
  s_cached = (_aq_from_min < _aq_to_min)
    ? (m >= _aq_from_min && m < _aq_to_min)
    : (m >= _aq_from_min || m < _aq_to_min);
  s_recompute_ms = now_ms + (60UL - t.tm_sec) * 1000UL;
  return s_cached;
}

// Sibling module so klaxon prefs render as their own UI block.
class KlaxonPrefs : public EGModule {
public:
  const char* name() override { return "klax"; }
  uint8_t display_order() override { return 19; }
  const EGPrefGroup* prefs_group() override;
  void on_prefs_loaded() override;
};

static KlaxonPrefs klaxonprefs;
EG_REGISTER_MODULE(klaxonprefs)

EG_PSTR(KX_L_EN,  "Klaxon on alert");
EG_PSTR(KX_H_EN,  "Sound a klaxon when CPM crosses the alert threshold");
EG_PSTR(KX_L_TY,  "Pattern");
EG_PSTR(KX_O_TY,  "Alarm|Two-Tone|Wail|Horn|Voice");
EG_PSTR(KX_L_VOL, "Volume");
EG_PSTR(KX_H_VOL, "0-100");
EG_PSTR(KX_L_CD,  "Cooldown");
EG_PSTR(KX_H_CD,  "Minutes between alert klaxons (1-60)");
EG_PSTR(KX_L_QFR, "Quiet from");
EG_PSTR(KX_H_QFR, "Silence klaxon + voice from this time (blank = off)");
EG_PSTR(KX_L_QTO, "Quiet to");
EG_PSTR(KX_H_QTO, "End of quiet window; crosses midnight if from > to");

static const EGPref KLAXON_PREF_ITEMS[] = {
  {"enable",     KX_L_EN,  KX_H_EN,  "0",  nullptr, 0, 0,   0, EGP_BOOL, 0},
  {"type",       KX_L_TY,  nullptr,  "0",  KX_O_TY, 0, 4,   0, EGP_ENUM, 0},
  {"volume",     KX_L_VOL, KX_H_VOL, "55", nullptr, 1, 100, 0, EGP_UINT, 0},
  {"cooldown",   KX_L_CD,  KX_H_CD,  "5",  nullptr, 1, 60,  0, EGP_UINT, EGP_ADVANCED},
  {"quiet_from", KX_L_QFR, KX_H_QFR, "",   nullptr, 0, 0,   5, EGP_STRING, EGP_TIME},
  {"quiet_to",   KX_L_QTO, KX_H_QTO, "",   nullptr, 0, 0,   5, EGP_STRING, EGP_TIME},
};

static const EGPrefGroup KLAXON_PREF_GROUP = {
  "klax", "Alert Klaxon", 1,
  KLAXON_PREF_ITEMS,
  sizeof(KLAXON_PREF_ITEMS) / sizeof(KLAXON_PREF_ITEMS[0]),
  EGP_CAT_OUTPUT, "enable",
};

const EGPrefGroup* KlaxonPrefs::prefs_group() { return &KLAXON_PREF_GROUP; }

void KlaxonPrefs::on_prefs_loaded() {
  // Forward so Voice's cached _klax_* members pick up runtime saves.
  voice.on_prefs_loaded();
}

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

static const VocabADPCM* vocab_find(const char* name) {
  if (!name) return nullptr;
  for (size_t i = 0; i < VOCAB_ADPCM_COUNT; i++) {
    if (strcmp(VOCAB_ADPCM_TABLE[i].name, name) == 0) {
      return &VOCAB_ADPCM_TABLE[i];
    }
  }
  return nullptr;
}

// Phase-accumulator upsampler from VOCAB_ADPCM_RATE (7350) to 22050 Hz
// I2S. phase_step = 1/3 - each ADPCM sample held for exactly 3 output
// samples (clean integer ratio, no stair-step jitter). pitch_scale > 1
// advances phase faster - shorter, higher-pitched playback (rising-
// fifth on the last digit of a Poacher 5-group).
static void adpcm_speak_word(const VocabADPCM* info, float pitch_scale) {
  if (!info || !info->data) return;

  adpcm_pred = 0;
  adpcm_idx  = 0;

  const float phase_step = (float)VOCAB_ADPCM_RATE * pitch_scale / 22050.0f;
  float phase = 0.0f;

  // Runtime volume: integer scale in 0..256. 256 = unity (no math change
  // for the common 100% case).
  const uint16_t vol = (uint16_t)voice.voiceVolume();
  const uint16_t scale_q8 = (vol >= 100) ? 256 : (uint16_t)((vol * 256u) / 100u);
  auto scaled = [scale_q8](int16_t s) -> int16_t {
    if (scale_q8 == 256) return s;
    return (int16_t)(((int32_t)s * scale_q8) >> 8);
  };

  constexpr int OUT_CHUNK = 256;
  int16_t out[OUT_CHUNK];
  int oc = 0;

  // audioop.lin2adpcm packs HIGH nibble first (sample 0), LOW second
  // (sample 1) per byte. Decoder must read in the same order.
  uint8_t pending_byte = pgm_read_byte(&info->data[0]);
  int16_t sample = adpcm_decode_nibble((pending_byte >> 4) & 0x0F);
  bool need_low = true;
  uint16_t byte_pos = 0;

  uint32_t total_in = info->byte_count * 2;
  uint32_t consumed = 1;
  while (consumed <= total_in) {
    out[oc++] = scaled(sample);
    if (oc >= OUT_CHUNK) {
      size_t w;
      i2s_write(I2S_NUM_0, out, OUT_CHUNK * sizeof(int16_t), &w,
                200 / portTICK_PERIOD_MS);
      oc = 0;
    }
    phase += phase_step;
    while (phase >= 1.0f && consumed < total_in) {
      phase -= 1.0f;
      if (need_low) {
        sample = adpcm_decode_nibble(pending_byte & 0x0F);
        need_low = false;
      } else {
        byte_pos++;
        if (byte_pos >= info->byte_count) {
          consumed = total_in;
          break;
        }
        pending_byte = pgm_read_byte(&info->data[byte_pos]);
        sample = adpcm_decode_nibble((pending_byte >> 4) & 0x0F);
        need_low = true;
      }
      consumed++;
    }
    if (consumed >= total_in) break;
  }
  // The inner decode runs ahead of the outer write - at HIFI (phase_step
  // = 1.0) the final decoded sample ends up in adpcm_pred but is never
  // written to output. That leaves a sample-sized step between the last
  // written value and tail = adpcm_pred which the fade starts from. Write
  // it explicitly here so the fade is continuous with what came before.
  int16_t final_sample = scaled((int16_t)adpcm_pred);
  if (oc < OUT_CHUNK) {
    out[oc++] = final_sample;
  } else {
    size_t w;
    i2s_write(I2S_NUM_0, &final_sample, sizeof(int16_t), &w,
              200 / portTICK_PERIOD_MS);
  }
  if (oc > 0) {
    size_t w;
    i2s_write(I2S_NUM_0, out, oc * sizeof(int16_t), &w,
              200 / portTICK_PERIOD_MS);
  }
  // 20 ms tail fade so the digit-to-silence boundary has no DC step.
  // tail must use the scaled value to match the last written sample, else
  // a vol < 100 produces a sudden jump up to raw adpcm_pred at fade start.
  constexpr int FADE_SAMPLES = 22050 * 20 / 1000;
  int16_t fade_buf[FADE_SAMPLES];
  const int16_t tail = scaled(adpcm_pred);
  for (int i = 0; i < FADE_SAMPLES; i++) {
    fade_buf[i] = (int16_t)((int32_t)tail * (FADE_SAMPLES - 1 - i) / (FADE_SAMPLES - 1));
  }
  size_t w;
  i2s_write(I2S_NUM_0, fade_buf, FADE_SAMPLES * sizeof(int16_t), &w,
            200 / portTICK_PERIOD_MS);
}

// ---------- Klaxon synthesis helpers ----------

// Square wave tone at freq_hz for ms with brief attack/release ramps.
// peak is the int16 ceiling (0..32767).
static void klaxon_square_tone(uint16_t freq_hz, uint16_t ms, int peak) {
  if (freq_hz == 0 || ms == 0) return;
  constexpr int OUT_CHUNK = 256;
  int16_t out[OUT_CHUNK];
  int oc = 0;
  const uint32_t half_period = (22050 / freq_hz) / 2;
  uint32_t phase = 0;
  const uint32_t total = (uint32_t)ms * 22050 / 1000;
  constexpr int ramp = 220;
  for (uint32_t i = 0; i < total; i++) {
    int env = peak;
    if (i < (uint32_t)ramp)         env = (int)i * peak / ramp;
    else if (i > total - ramp)      env = (int)(total - i) * peak / ramp;
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

// Linear-interpolated square-wave pitch sweep from f_start to f_end over ms.
// peak is the int16 ceiling (0..32767).
static void klaxon_square_sweep(float f_start, float f_end, uint16_t ms, int peak) {
  if (ms == 0) return;
  constexpr int OUT_CHUNK = 256;
  int16_t out[OUT_CHUNK];
  int oc = 0;
  constexpr float inv_fs = 1.0f / 22050.0f;
  float phase = 0.0f;
  const uint32_t total = (uint32_t)ms * 22050 / 1000;
  constexpr int ramp = 220;
  const float fr = (float)total;
  for (uint32_t i = 0; i < total; i++) {
    const float t = (float)i / fr;
    const float f = f_start + (f_end - f_start) * t;
    phase += f * inv_fs;
    if (phase >= 1.0f) phase -= 1.0f;
    int env = peak;
    if (i < (uint32_t)ramp)         env = (int)i * peak / ramp;
    else if (i > total - ramp)      env = (int)(total - i) * peak / ramp;
    int16_t s = (phase < 0.5f) ? env : -env;
    out[oc++] = s;
    if (oc >= OUT_CHUNK) {
      size_t w;
      i2s_write(I2S_NUM_0, out, OUT_CHUNK * sizeof(int16_t), &w,
                200 / portTICK_PERIOD_MS);
      oc = 0;
    }
  }
  if (oc > 0) {
    size_t w;
    i2s_write(I2S_NUM_0, out, oc * sizeof(int16_t), &w,
              200 / portTICK_PERIOD_MS);
  }
}

static void klaxon_silence(uint16_t ms) {
  if (ms == 0) return;
  constexpr int OUT_CHUNK = 256;
  static const int16_t zeros[OUT_CHUNK] = {0};
  uint32_t remaining = (uint32_t)ms * 22050 / 1000;
  while (remaining > 0) {
    const uint16_t n = (remaining > OUT_CHUNK) ? OUT_CHUNK : (uint16_t)remaining;
    size_t w;
    i2s_write(I2S_NUM_0, zeros, n * sizeof(int16_t), &w,
              100 / portTICK_PERIOD_MS);
    remaining -= n;
  }
}

// ---------- Voice public API ----------

void Voice::begin() {
  // Nothing to allocate; ADPCM tables live in PROGMEM.
}

static const char* DIGIT_NAMES[10] = {
  "zero", "one", "two", "three", "four",
  "five", "six", "seven", "eight", "nine"
};

void Voice::loop(unsigned long /*now*/) {
  if (_klax_en) {
    const bool now_alert = gcounter.is_alert();
    if (now_alert && !_prev_alert && !audioQuietNow()) {
      const unsigned long now_ms = fast_millis();
      const unsigned long cd_ms = (unsigned long)_klax_cd * 60UL * 1000UL;
      if (_last_klax_ms == 0 || (now_ms - _last_klax_ms) > cd_ms) {
        _last_klax_ms = now_ms;
        AudioCmd cmd{};
        cmd.type = AC_KLAXON; cmd.i1 = _klax_type;
        audio_enqueue(cmd);
      }
    }
    _prev_alert = now_alert;
  }
  if (_ann_en && !audioQuietNow()) {
    const unsigned long now_ms = fast_millis();
    const unsigned long iv_ms  = (unsigned long)_ann_iv * 1000UL;
    if (_last_ann_ms == 0 || (now_ms - _last_ann_ms) >= iv_ms) {
      _last_ann_ms = now_ms;
      AudioCmd cmd{};
      cmd.type = AC_ANNOUNCE;
      audio_enqueue(cmd);
    }
  }
  if (_ann_radmon && !audioQuietNow()) {
    EGModule* m = EGModuleRegistry::find("radmon");
    if (m && m->last_attempt_ms != 0 &&
        m->last_attempt_ms != _radmon_last_attempt) {
      const bool ok = m->last_ok;
      if (_radmon_known && ok != _radmon_was_ok) {
        AudioCmd cmd{};
        cmd.type = AC_WORD;
        strncpy(cmd.s, "RadMon", sizeof(cmd.s) - 1);
        audio_enqueue(cmd);
        cmd.type = AC_WORD;
        strncpy(cmd.s, ok ? "online" : "offline", sizeof(cmd.s) - 1);
        audio_enqueue(cmd);
      }
      _radmon_was_ok = ok;
      _radmon_known  = true;
      _radmon_last_attempt = m->last_attempt_ms;
    }
  }
}

void Voice::announce() {
  if (gcounter.is_alert()) {
    speakWord("alert"); silence(200);
  } else if (gcounter.is_warning()) {
    speakWord("warning"); silence(200);
  }
  speakWord("CPM"); silence(150);
  speakNumber(gcounter.get_cpm());
  if (_ann_usv) {
    silence(250);
    const float usv = gcounter.get_usv();
    // >=1000 micro = >=1 milli sievert per hour. Switch units rather than
    // counting thousands of micro.
    const bool use_milli = (usv >= 1000.0f);
    const float scaled = use_milli ? (usv / 1000.0f) : usv;
    const long tenths = (long)(scaled * 10.0f + 0.5f);
    speakNumber(tenths / 10);
    silence(80);
    speakWord("point"); silence(80);
    speakWord(DIGIT_NAMES[tenths % 10]); silence(120);
    speakWord(use_milli ? "milli" : "micro"); silence(60);
    speakWord("sievert"); silence(60);
    speakWord("per");     silence(60);
    speakWord("hour");
  }
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

void Voice::speakWord(const char* name, float pitch_scale) {
  adpcm_speak_word(vocab_find(name), pitch_scale);
}

void Voice::speakDigit(uint8_t d, bool rising) {
  if (d > 9) return;
  speakWord(DIGIT_NAMES[d], rising ? 1.1f : 1.0f);  // rising-fifth on last digit
}

void Voice::say(const char* text) {
  speakDigits(text);
}

static const char* TEENS_NAMES[10] = {
  "ten","eleven","twelve","thirteen","fourteen",
  "fifteen","sixteen","seventeen","eighteen","nineteen"
};
static const char* TENS_NAMES[8] = {
  "twenty","thirty","forty","fifty","sixty","seventy","eighty","ninety"
};

// 0..999 - caller handles a bare 0 (this never emits "zero" on its own).
static void voice_speak_below_thousand(Voice& v, long n) {
  if (n >= 100) {
    long h = n / 100;
    v.speakWord(DIGIT_NAMES[h]); v.silence(80);
    v.speakWord("hundred");      v.silence(120);
    n %= 100;
    if (n == 0) return;
  }
  if (n >= 20) {
    long t = n / 10;
    v.speakWord(TENS_NAMES[t - 2]); v.silence(80);
    n %= 10;
  } else if (n >= 10) {
    v.speakWord(TEENS_NAMES[n - 10]); v.silence(80);
    return;
  }
  if (n > 0) {
    v.speakWord(DIGIT_NAMES[n]); v.silence(80);
  }
}

void Voice::speakNumber(long n) {
  if (n < 0) n = -n;
  if (n == 0) { speakWord("zero"); silence(80); return; }
  if (n >= 1000000) {
    // Vocab tops out at "thousand"; for >=1M fall back to numbers-station
    // digit grouping (5-per-group, rising fifth on the 5th, longer break).
    char buf[16];
    snprintf(buf, sizeof(buf), "%ld", n);
    const size_t len = strlen(buf);
    for (size_t i = 0; i < len; i++) {
      const bool last_in_group = ((i + 1) % 5 == 0) || (i + 1 == len);
      speakDigit((uint8_t)(buf[i] - '0'), last_in_group);
      silence(last_in_group ? 600 : 200);
    }
    return;
  }
  if (n >= 1000) {
    long thousands = n / 1000;
    voice_speak_below_thousand(*this, thousands);
    speakWord("thousand"); silence(120);
    n %= 1000;
    if (n == 0) return;
  }
  voice_speak_below_thousand(*this, n);
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
  // All routes enqueue and return immediately. The audio worker (started in
  // AudioTick::begin) drains the queue in order, so requests "line up" on
  // the I2S bus without blocking the HTTP thread.

  // GET /adpcm?w=<word>  - speak a vocab word
  // GET /adpcm?n=N       - speak a natural number
  http.on("/adpcm", EGHttpRequest::GET,
    [](EGHttpRequest& req, EGHttpResponse& res, void*) {
      AudioCmd cmd{};
      const char* wArg = req.arg("w");
      if (wArg && wArg[0]) {
        if (!vocab_find(wArg)) { res.send(404, "text/plain", "unknown word"); return; }
        cmd.type = AC_WORD;
        strncpy(cmd.s, wArg, sizeof(cmd.s) - 1);
      } else if (const char* nArg = req.arg("n"); nArg && nArg[0]) {
        cmd.type = AC_NUMBER; cmd.i32 = atol(nArg);
      } else {
        res.send(400, "text/plain", "w= or n= required");
        return;
      }
      if (audio_enqueue(cmd)) res.send(200, "text/plain", "queued");
      else                    res.send(503, "text/plain", "queue full");
    });

  // GET /klaxon       - play configured klaxon
  // GET /klaxon?t=N   - test pattern 0..4
  http.on("/klaxon", EGHttpRequest::GET,
    [](EGHttpRequest& req, EGHttpResponse& res, void*) {
      const char* tArg = req.arg("t");
      uint8_t type = voice.klaxonType();
      if (tArg && tArg[0]) {
        int v = atoi(tArg);
        if (v < 0 || v > 4) { res.send(400, "text/plain", "t=0..4"); return; }
        type = (uint8_t)v;
      }
      AudioCmd cmd{};
      cmd.type = AC_KLAXON; cmd.i1 = type;
      if (audio_enqueue(cmd)) res.send(200, "text/plain", "queued");
      else                    res.send(503, "text/plain", "queue full");
    });
}

// playKlaxon picks the active volume from _klax_vol and dispatches to
// the selected pattern. Caller is responsible for I2S ownership.
void Voice::playKlaxon(uint8_t type) {
  const int peak = (int)((uint32_t)_klax_vol * 32767 / 100);
  switch (type) {
    case 0: default:
      // SKALA (Chernobyl alarm) - 1040 Hz @ 350 ms alternating with 680
      // Hz @ 300 ms, measured by FFT on the reference recording. 9 cycles.
      for (int c = 0; c < 9; c++) {
        klaxon_square_tone(1040, 350, peak);
        klaxon_square_tone(680,  300, peak);
      }
      break;
    case 1: {
      // Euro 2-tone (police/ambulance) - 410/610 Hz @ 700 ms each.
      const uint16_t tones[] = { 410, 610 };
      for (int c = 0; c < 5; c++)
        for (uint16_t f : tones) klaxon_square_tone(f, 700, peak);
      break;
    }
    case 2:
      // WWII air-raid siren wail. Motor-driven sirens (Carter/Hochiki) take
      // 4-7 sec to swing through their range. 250 Hz - 750 Hz across a 4 sec
      // up + 4 sec down cycle, repeated twice. Total ~16 sec.
      for (int c = 0; c < 2; c++) {
        klaxon_square_sweep(250.0f, 750.0f, 4000, peak);
        klaxon_square_sweep(750.0f, 250.0f, 4000, peak);
      }
      break;
    case 3:
      // Naval AOOGA - alternating low/high square tones (no sweep, two
      // discrete pitches). High ~600 Hz, low ~300 Hz, ~400 ms each, with
      // short silence between pairs for the iconic "AAH-OOO-GAH AAH-OOO-GAH".
      for (int c = 0; c < 5; c++) {
        klaxon_square_tone(600, 380, peak);
        klaxon_square_tone(300, 380, peak);
        klaxon_silence(140);
      }
      break;
    case 4:
      // Cori voice alert: "alert alert alert" with pauses. Uses the spoken
      // vocab; the klaxon volume pref does NOT apply here - this rides on
      // voice_volume instead, which is also user-configurable.
      for (int c = 0; c < 3; c++) {
        speakWord("alert");
        silence(300);
      }
      break;
  }
}

#endif
