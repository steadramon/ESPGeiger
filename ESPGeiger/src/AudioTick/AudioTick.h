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
#include <driver/i2s.h>
#include "../Module/EGModule.h"


#ifndef AUDIO_TICK_BCLK
#define AUDIO_TICK_BCLK 41
#endif
#ifndef AUDIO_TICK_WS
#define AUDIO_TICK_WS   42
#endif
#ifndef AUDIO_TICK_DOUT
#define AUDIO_TICK_DOUT 40
#endif

#define AUDIO_TICK_SAMPLE_RATE 22050

// HTTP routes + alert paths enqueue here; a single worker drives I2S.
enum AudioCmdType : uint8_t {
  AC_WORD = 1,
  AC_NUMBER,
  AC_KLAXON,
  AC_ANNOUNCE,
  AC_NUMBERS,
  AC_CHIME,
  AC_CLICK,
};

struct AudioCmd {
  uint8_t type;
  uint8_t i1;
  int32_t i32;
  char    s[24];
};

bool audio_enqueue(const AudioCmd& cmd);

class AudioTick : public EGModule {
  public:
    AudioTick() {}
    const char* name() override { return "tick"; }
    uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
    uint8_t display_order() override { return 17; }
    uint16_t warmup_seconds() override { return 0; }
    bool has_loop() override { return false; }
    void begin() override;
    const EGPrefGroup* prefs_group() override;
    void on_prefs_loaded() override;
    void registerRoutes(EGHttpServer& http) override;

    // Called from Counter when a pulse arrives. Token-bucket-throttled so
    // a high-rate source can't saturate the I2S TX buffer.
    void notifyClick(unsigned long now_ms);

    // Acquire/release exclusive I2S ownership for long-running playback
    // (numbers station, Klatt digit synth). While owned, ticks are dropped.
    static void setI2SOwned(bool owned);
    static bool isI2SOwned();
    bool        isI2SUp() const { return _i2s_up; }
    void        dispatch(const struct AudioCmd& cmd);
    static void numbersPad(uint8_t* out, size_t n);

  private:
    void playClick(uint8_t engine_override = 255);
    void playClickPool();
    void playClickChirp();
    void playBootChime();
    // Trigger a chime by index. 0=off, 1=random, 2..8=specific.
    void play_chime_now(uint8_t c);
    void chimeThreeBeeps();
    void chimeFolkMelody();
    void chimeDtmf();
    void chimeBitPing();
    void chimeMarioCoin();
    void chimeSputnik();
    void chimeItemGet();
    void chimeMorseCallsign();    // chime 9
    void chimeNumbersStation();
    void playMelodyNote(uint16_t freq_hz, uint16_t ms, bool square = false,
                        uint16_t gap_ms = 30);

    bool          _enabled  = false;
    bool          _i2s_up   = false;
    uint8_t       _volume   = 60;
    uint16_t      _freq_hz  = 900;
    uint8_t       _decay_ms = 4;
    uint8_t       _engine   = 0;
    uint8_t       _peak     = 85;   // amplitude ceiling 1-100% (NS4168 NCN-safe at 85)
    uint8_t       _chime    = 1;    // 0=off, 1=random, 2+ = specific
    uint8_t       _bclk     = AUDIO_TICK_BCLK;
    uint8_t       _ws       = AUDIO_TICK_WS;
    uint8_t       _dout     = AUDIO_TICK_DOUT;
    unsigned long _last_emit_ms = 0;
    uint8_t       _tokens = 5;
    unsigned long _last_token_ms = 0;
};

extern AudioTick audiotick;

#endif
#endif
