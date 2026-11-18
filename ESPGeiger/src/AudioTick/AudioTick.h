/*
  AudioTick.h - I2S-driven audible Geiger click.

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
#ifndef AUDIOTICK_H
#define AUDIOTICK_H
#ifdef AUDIO_TICK

#include <Arduino.h>
#include <atomic>
#include <driver/i2s.h>
#include "Corium/Module/Module.h"


#ifndef AUDIO_TICK_BCLK
#define AUDIO_TICK_BCLK 41
#endif
#ifndef AUDIO_TICK_WS
#define AUDIO_TICK_WS   42
#endif
#ifndef AUDIO_TICK_DOUT
#define AUDIO_TICK_DOUT 40
#endif

// S3 plays ALL_LEFT at 2x speed
#ifndef AUDIO_TICK_CHANNEL_FMT
#ifdef CONFIG_IDF_TARGET_ESP32S3
#define AUDIO_TICK_CHANNEL_FMT I2S_CHANNEL_FMT_ONLY_LEFT
#else
#define AUDIO_TICK_CHANNEL_FMT I2S_CHANNEL_FMT_ALL_LEFT
#endif
#endif

// Hz, 16 bit mono
#define AUDIO_TICK_SAMPLE_RATE 22050

constexpr uint32_t ms_to_samples(uint32_t ms) {
  return ms * AUDIO_TICK_SAMPLE_RATE / 1000;
}

// one worker task owns I2S
enum AudioCmdType : uint8_t {
  AC_WORD = 1,
  AC_NUMBER,
  AC_KLAXON,
  AC_ANNOUNCE,
  AC_NUMBERS,
  AC_CHIME,
  AC_CLICK,
  AC_DIGIT,
  AC_SILENCE,
};

struct AudioCmd {
  uint8_t type;
  uint8_t i1;
  int32_t i32;
  const char* word;   // static, vocab name
  bool web;           // from a web route, only one queued at a time
};

// Safe from any task
bool audio_enqueue(const AudioCmd& cmd);
extern const char AT_QUEUED[];   // the routes' replies, PROGMEM
extern const char AT_QFULL[];
void audio_silence(uint16_t ms);
// Speech commands are handed to this, on the worker task
typedef void (*AudioFn)(const AudioCmd& cmd);
void audio_speech(AudioFn fn);

class AudioTick : public corium::Module {
  public:
    AudioTick() {}
    const char* name() override { return PSTR("tick"); }
    uint8_t priority() override { return CORIUM_PRIORITY_HARDWARE; }
    uint8_t displayOrder() override { return 17; }
    void loop(unsigned long now) override;
    void begin() override;
    const corium::PrefGroup* prefsGroup() override;
    void onPrefsLoaded() override;
    void registerRoutes(EGHttpServer& http) override;

    void notifyClick(unsigned long now_ms);

    // Clicks are dropped while something else owns I2S
    static void setI2SOwned(bool owned);
    bool        isI2SUp() const { return _i2s_up; }
    void        dispatch(const struct AudioCmd& cmd);
    static void numbersPad(uint8_t* out, size_t n);

    bool muted() const { return _muted.load(std::memory_order_relaxed); }
    // Runs on the timer task, loop() gets posted to log it
    void toggleMute() { _muted.store(!muted(), std::memory_order_relaxed); }

  private:
    void playClick(uint8_t engine = 255);
    void playClickPool();
    void playClickChirp();
    void synthClick(float asym, float f2_amp, float amp, float tau, float f_lo, uint32_t nr);
    void playBootChime();
    void play_chime_now(uint8_t c);
    void chimeThreeBeeps();
    void chimeFolkMelody();
    void chimeDtmf();
    void chimeBitPing();
    void chimeMarioCoin();
    void chimeSputnik();
    void chimeItemGet();
    void chimeMorseCallsign();
    void chimeNumbersStation();
    void playMelodyNote(uint16_t freq_hz, uint16_t ms, bool square = false,
                        uint16_t gap_ms = 30);

    bool          _enabled  = false;
    bool          _i2s_up   = false;
    uint8_t       _volume   = 60;
    uint16_t      _freq_hz  = 900;
    uint8_t       _decay_ms = 4;
    uint8_t       _engine   = 0;
    uint8_t       _peak     = 85;   // %, 85 safe for NS4168 NCN
    uint8_t       _chime    = 1;    // 0 = off, 1 = random
    uint8_t       _bclk     = AUDIO_TICK_BCLK;
    uint8_t       _ws       = AUDIO_TICK_WS;
    uint8_t       _dout     = AUDIO_TICK_DOUT;
    uint8_t       _tokens = 5;
    uint32_t      _last_token_ms = 0;
    std::atomic<bool> _muted{false};
    bool          _muted_logged = false;
};

extern AudioTick audiotick;

#endif
#endif
