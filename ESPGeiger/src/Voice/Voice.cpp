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
#include "../Corium/Logger/Logger.h"
#include "../Corium/Module/EGModuleRegistry.h"
#include "../Corium/Prefs/EGPrefs.h"
#include "../Corium/System/FastMillis.h"
#include "../Corium/System/Globals.h"
#include "../Corium/Util/StringUtil.h"
#include <EGHttpServer.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "vocab_adpcm.h"
#include "../AudioTick/AudioTick.h"

EG_PSTR(K_ANNOUNCE_ENABLE,   "announce_enable");
EG_PSTR(K_ANNOUNCE_INTERVAL, "announce_interval");
EG_PSTR(K_ANNOUNCE_USV,      "announce_usv");
EG_PSTR(K_ANNOUNCE_RADMON,   "announce_radmon");
EG_PSTR(K_VOICE_VOLUME,      "voice_volume");
EG_PSTR(K_COOLDOWN,          "cooldown");

Voice voice;
EG_REGISTER_MODULE(voice)

EG_PSTR(VX_L_AEN, "Announce");
EG_PSTR(VX_H_AEN, "Periodically speak the current CPM");
EG_PSTR(VX_L_AIV, "Every (seconds)");
EG_PSTR(VX_H_AIV, "10-3600");
EG_PSTR(VX_L_AUS, "Announce dose");
EG_PSTR(VX_H_AUS, "Include micro sievert per hour with the announcement");
EG_PSTR(VX_L_ARM, "RadMon up/down");
EG_PSTR(VX_H_ARM, "Speak when RadMon goes online or offline");

static const EGPref VOICE_PREF_ITEMS[] PROGMEM = {
  {K_ANNOUNCE_ENABLE,   VX_L_AEN,  VX_H_AEN,  "0",  nullptr,  0,    0,  0, EGP_BOOL, 0},
  {K_ANNOUNCE_INTERVAL, VX_L_AIV,  VX_H_AIV,  "60", nullptr, 10, 3600,  0, EGP_UINT, EGP_ADVANCED},
  {K_ANNOUNCE_USV,      VX_L_AUS,  VX_H_AUS,  "0",  nullptr,  0,    0,  0, EGP_BOOL, 0},
  {K_ANNOUNCE_RADMON,   VX_L_ARM,  VX_H_ARM,  "0",  nullptr,  0,    0,  0, EGP_BOOL, 0},
  {K_VOICE_VOLUME,      EG_L_VOL,  EG_H_VOL,  "80", nullptr,  1,  100,  0, EGP_UINT, 0},
};

static const EGPrefGroup VOICE_PREF_GROUP PROGMEM = {
  "klaxon", "Voice", 1,
  VOICE_PREF_ITEMS,
  sizeof(VOICE_PREF_ITEMS) / sizeof(VOICE_PREF_ITEMS[0]),
  EGP_CAT_OUTPUT, K_ANNOUNCE_ENABLE,
};

const EGPrefGroup* Voice::prefs_group() { return &VOICE_PREF_GROUP; }

void Voice::on_prefs_loaded() {
  _voice_vol = (uint8_t)EGPrefs::getUInt("klaxon", K_VOICE_VOLUME);
  _ann_en     = EGPrefs::getBool("klaxon", K_ANNOUNCE_ENABLE);
  _ann_iv     = (uint16_t)EGPrefs::getUInt("klaxon", K_ANNOUNCE_INTERVAL);
  _ann_usv    = EGPrefs::getBool("klaxon", K_ANNOUNCE_USV);
  _ann_radmon = EGPrefs::getBool("klaxon", K_ANNOUNCE_RADMON);
  _klax_en   = EGPrefs::getBool("klax", EG_K_ENABLE);
  _klax_type = (uint8_t)EGPrefs::getUInt("klax", EG_K_TYPE);
  _klax_vol  = (uint8_t)EGPrefs::getUInt("klax", EG_K_VOLUME);
  _klax_cd   = (uint8_t)EGPrefs::getUInt("klax", K_COOLDOWN);
  _quiet.set(EGPrefs::getString("klax", EG_K_QUIET_FROM),
             EGPrefs::getString("klax", EG_K_QUIET_TO));
}

class KlaxonPrefs : public EGModule {
public:
  const char* name() override { return PSTR("klax"); }
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
EG_PSTR(KX_L_CD,  "Cooldown");
EG_PSTR(KX_H_CD,  "Minutes between alert klaxons (1-60)");
EG_PSTR(KX_H_QFR, "Silence klaxon + voice from this time (blank = off)");

static const EGPref KLAXON_PREF_ITEMS[] PROGMEM = {
  {EG_K_ENABLE,     KX_L_EN,  KX_H_EN,  "0",  nullptr, 0, 0,   0, EGP_BOOL, 0},
  {EG_K_TYPE,       KX_L_TY,  nullptr,  "0",  KX_O_TY, 0, 4,   0, EGP_ENUM, 0},
  {EG_K_VOLUME,     EG_L_VOL, EG_H_VOL, "55", nullptr, 1, 100, 0, EGP_UINT, 0},
  {K_COOLDOWN,      KX_L_CD,  KX_H_CD,  "5",  nullptr, 1, 60,  0, EGP_UINT, EGP_ADVANCED},
  {EG_K_QUIET_FROM, EG_L_QFR, KX_H_QFR, "",   nullptr, 0, 0,   EG_HHMM_MAX, EGP_STRING, EGP_TIME},
  {EG_K_QUIET_TO,   EG_L_QTO, EG_H_QTO, "",   nullptr, 0, 0,   EG_HHMM_MAX, EGP_STRING, EGP_TIME},
};

static const EGPrefGroup KLAXON_PREF_GROUP PROGMEM = {
  "klax", "Alert Klaxon", 1,
  KLAXON_PREF_ITEMS,
  sizeof(KLAXON_PREF_ITEMS) / sizeof(KLAXON_PREF_ITEMS[0]),
  EGP_CAT_OUTPUT, EG_K_ENABLE,
};

const EGPrefGroup* KlaxonPrefs::prefs_group() { return &KLAXON_PREF_GROUP; }

void KlaxonPrefs::on_prefs_loaded() {
  voice.on_prefs_loaded();
}

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

// 256 = unity
static uint16_t voice_scale_q8() {
  const uint16_t vol = (uint16_t)voice.voiceVolume();
  return (vol >= 100) ? 256 : (uint16_t)((vol * 256u) / 100u);
}

static int16_t scale_sample(int16_t s, uint16_t scale_q8) {
  if (scale_q8 == 256) return s;
  return (int16_t)(((int32_t)s * scale_q8) >> 8);
}

// decode runs one ahead, write the last one
static void adpcm_flush(int16_t* out, int oc, int out_cap, int16_t final_sample) {
  if (oc < out_cap) {
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
}

// tail must be the scaled value
static void adpcm_fade_out(int16_t tail) {
  constexpr int FADE_SAMPLES = 22050 * 20 / 1000;
  int16_t fade_buf[FADE_SAMPLES];
  for (int i = 0; i < FADE_SAMPLES; i++) {
    fade_buf[i] = (int16_t)((int32_t)tail * (FADE_SAMPLES - 1 - i) / (FADE_SAMPLES - 1));
  }
  size_t w;
  i2s_write(I2S_NUM_0, fade_buf, FADE_SAMPLES * sizeof(int16_t), &w,
            200 / portTICK_PERIOD_MS);
}

// 7350 Hz up to 22050, each sample held 3x
static void adpcm_speak_word(const VocabADPCM* info, float pitch_scale) {
  if (!info || !info->data) return;

  adpcm_pred = 0;
  adpcm_idx  = 0;

  const float phase_step = (float)VOCAB_ADPCM_RATE * pitch_scale / 22050.0f;
  float phase = 0.0f;

  const uint16_t scale_q8 = voice_scale_q8();

  constexpr int OUT_CHUNK = 256;
  int16_t out[OUT_CHUNK];
  int oc = 0;

  // high nibble first, as audioop.lin2adpcm
  uint8_t pending_byte = pgm_read_byte(&info->data[0]);
  int16_t sample = adpcm_decode_nibble((pending_byte >> 4) & 0x0F);
  bool need_low = true;
  uint16_t byte_pos = 0;

  uint32_t total_in = info->byte_count * 2;
  uint32_t consumed = 1;
  while (consumed <= total_in) {
    out[oc++] = scale_sample(sample, scale_q8);
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
  adpcm_flush(out, oc, OUT_CHUNK, scale_sample((int16_t)adpcm_pred, scale_q8));
  adpcm_fade_out(scale_sample(adpcm_pred, scale_q8));
}

// peak 0..32767
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

void Voice::begin() {
}

static const char* DIGIT_NAMES[10] = {
  "zero", "one", "two", "three", "four",
  "five", "six", "seven", "eight", "nine"
};

void Voice::queueKlaxonOnAlert() {
  const bool now_alert = gcounter.is_alert();
  if (now_alert && !_prev_alert && !_quiet.active()) {
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

void Voice::queueAnnouncement() {
  const unsigned long now_ms = fast_millis();
  const unsigned long iv_ms  = (unsigned long)_ann_iv * 1000UL;
  if (_last_ann_ms == 0 || (now_ms - _last_ann_ms) >= iv_ms) {
    _last_ann_ms = now_ms;
    AudioCmd cmd{};
    cmd.type = AC_ANNOUNCE;
    audio_enqueue(cmd);
  }
}

// not at boot
void Voice::queueRadmonChange() {
  EGModule* m = EGModuleRegistry::find("radmon");
  if (m && m->last_attempt_ms != 0 &&
      m->last_attempt_ms != _radmon_last_attempt) {
    const bool ok = m->last_ok;
    if (_radmon_known && ok != _radmon_was_ok) {
      AudioCmd cmd{};
      cmd.type = AC_WORD;
      cmd.word = "RadMon";
      audio_enqueue(cmd);
      cmd.word = ok ? "online" : "offline";
      audio_enqueue(cmd);
    }
    _radmon_was_ok = ok;
    _radmon_known  = true;
    _radmon_last_attempt = m->last_attempt_ms;
  }
}

void Voice::loop(unsigned long /*now*/) {
  if (_klax_en) queueKlaxonOnAlert();
  if (_ann_en && !_quiet.active()) queueAnnouncement();
  if (_ann_radmon && !_quiet.active()) queueRadmonChange();
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
  static const int16_t zeros[CHUNK] = {0};
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
  speakWord(DIGIT_NAMES[d], rising ? 1.1f : 1.0f);
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

// caller handles 0
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
    // no word past thousand, digit groups
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

// all just enqueue, audio worker plays in order
static void send_queued(EGHttpResponse& res, const AudioCmd& cmd) {
  if (audio_enqueue(cmd)) res.send(200, FPSTR(HTTP_CT_PLAIN), F("queued"));
  else                    res.send(503, FPSTR(HTTP_CT_PLAIN), F("queue full"));
}

static void hAdpcm(EGHttpRequest& req, EGHttpResponse& res, void*) {
  AudioCmd cmd{};
  const char* wArg = req.arg(F("w"));
  if (wArg && wArg[0]) {
    const VocabADPCM* v = vocab_find(wArg);
    if (!v) { res.send(404, FPSTR(HTTP_CT_PLAIN), F("unknown word")); return; }
    cmd.type = AC_WORD;
    cmd.word = v->name;   // arg buffer gets reused
  } else if (parse_i32(req.arg(F("n")), &cmd.i32)) {
    cmd.type = AC_NUMBER;
  } else {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F("w= or n= required"));
    return;
  }
  send_queued(res, cmd);
}

static void hKlaxon(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const char* tArg = req.arg(F("t"));
  uint8_t type = voice.klaxonType();
  if (tArg && tArg[0]) {
    int32_t v;
    if (!parse_i32(tArg, &v) || v < 0 || v > 4) { res.send(400, FPSTR(HTTP_CT_PLAIN), F("t=0..4")); return; }
    type = (uint8_t)v;
  }
  AudioCmd cmd{};
  cmd.type = AC_KLAXON; cmd.i1 = type;
  send_queued(res, cmd);
}

void Voice::registerRoutes(EGHttpServer& http) {
  http.on(F("/adpcm"),  EGHttpRequest::GET, hAdpcm);
  http.on(F("/klaxon"), EGHttpRequest::GET, hKlaxon);
}

// caller owns I2S
void Voice::playKlaxon(uint8_t type) {
  const int peak = (int)((uint32_t)_klax_vol * 32767 / 100);
  switch (type) {
    case 0: default:
      for (int c = 0; c < 9; c++) {
        klaxon_square_tone(1040, 350, peak);
        klaxon_square_tone(680,  300, peak);
      }
      break;
    case 1: {
      const uint16_t tones[] = { 410, 610 };
      for (int c = 0; c < 5; c++)
        for (uint16_t f : tones) klaxon_square_tone(f, 700, peak);
      break;
    }
    case 2:
      for (int c = 0; c < 2; c++) {
        klaxon_square_sweep(250.0f, 750.0f, 4000, peak);
        klaxon_square_sweep(750.0f, 250.0f, 4000, peak);
      }
      break;
    case 3:
      for (int c = 0; c < 5; c++) {
        klaxon_square_tone(600, 380, peak);
        klaxon_square_tone(300, 380, peak);
        klaxon_silence(140);
      }
      break;
    case 4:
      // voice volume, not klaxon
      for (int c = 0; c < 3; c++) {
        speakWord("alert");
        silence(300);
      }
      break;
  }
}

#endif
