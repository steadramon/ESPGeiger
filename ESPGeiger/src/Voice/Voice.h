/*
  Voice.h - Cori (Piper en_GB) digit playback over the AudioTick I2S amp.

  Copyright (C) 2026 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/
#ifndef VOICE_H
#define VOICE_H
#ifdef AUDIO_TICK

#include <Arduino.h>
#include "../Module/EGModule.h"

class Voice : public EGModule {
  public:
    Voice() {}
    const char* name() override { return "voice"; }
    uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
    uint8_t display_order() override { return 18; }
    uint16_t warmup_seconds() override { return 0; }
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return 1000; }
    void begin() override;
    void loop(unsigned long now) override;
    void registerRoutes(EGHttpServer& http) override;

    const EGPrefGroup* prefs_group() override;
    void on_prefs_loaded() override;

    void speakDigit(uint8_t d, bool rising);
    void speakDigits(const char* s);
    void speakWord(const char* name, float pitch_scale = 1.0f);
    // Natural reading up to 999,999; falls back to digit groups beyond.
    void speakNumber(long n);
    void say(const char* text);
    void silence(uint16_t ms);

    // 0=reactor, 1=police, 2=air raid, 3=naval AOOGA, 4=Cori voice
    void playKlaxon(uint8_t type);

    uint8_t klaxonType()    const { return _klax_type; }
    uint8_t klaxonVolume()  const { return _klax_vol; }

    void announce();  // CPM plus state prefix when not normal

  private:
    bool     _klax_en   = false;
    uint8_t  _klax_type = 0;
    uint8_t  _klax_vol  = 55;
    uint8_t  _klax_cd   = 5;
    uint8_t  _voice_vol = 80;
    bool     _ann_en    = false;
    uint16_t _ann_iv    = 60;
    bool     _ann_usv   = false;
    bool     _ann_radmon = false;
    bool     _radmon_known  = false;  // have we seen an attempt yet?
    bool     _radmon_was_ok = true;
    unsigned long _radmon_last_attempt = 0;
    int16_t  _aq_from_min = -1;   // mins since midnight, -1 = off
    int16_t  _aq_to_min   = -1;
    bool     _prev_alert = false;
    unsigned long _last_klax_ms = 0;
    unsigned long _last_ann_ms  = 0;
    bool     audioQuietNow();
  public:
    uint8_t  voiceVolume() const { return _voice_vol; }
};

extern Voice voice;

#endif
#endif
