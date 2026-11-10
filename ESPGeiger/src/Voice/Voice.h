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

#include <stdint.h>
#include "Corium/Platform/Flash.h"
#include "Corium/Module/Module.h"
#include "Corium/Output/QuietWindow.h"

// Spoken cpm, alerts and klaxons; speak and play calls block on I2S
class Voice : public corium::Module {
  public:
    Voice() {}
    const char* name() override { return PSTR("voice"); }
    uint8_t priority() override { return CORIUM_PRIORITY_HARDWARE; }
    uint8_t displayOrder() override { return 18; }
    static constexpr uint16_t LOOP_MS = 1000;
    void begin() override;
    void loop(unsigned long now) override;
    void registerRoutes(EGHttpServer& http) override;

    const corium::PrefGroup* prefsGroup() override;
    void onPrefsLoaded() override;

    void speakDigit(uint8_t d, bool rising);
    void speakDigits(const char* s);
    void speakWord(const char* name, float pitch_scale = 1.0f);
    void speakNumber(long n);
    void say(const char* text);
    void silence(uint16_t ms);

    // 0=reactor, 1=police, 2=air raid, 3=naval AOOGA, 4=Cori voice
    void playKlaxon(uint8_t type);

    uint8_t klaxonType()    const { return _klax_type; }
    uint8_t klaxonVolume()  const { return _klax_vol; }

    void announce();

  private:
    bool     _klax_en   = false;
    uint8_t  _klax_type = 0;
    uint8_t  _klax_vol  = 55;
    uint8_t  _klax_min  = 5;
    uint8_t  _voice_vol = 80;
    bool     _ann_en    = false;
    uint16_t _ann_s     = 60;
    bool     _ann_usv   = false;
    bool     _ann_radmon = false;
    corium::Sender* _radmon = nullptr;   // nullptr = not built
    bool     _radmon_known  = false;
    bool     _radmon_was_ok = true;
    unsigned long _radmon_tried = 0;
    corium::QuietWindow _quiet;
    bool     _prev_alert = false;
    unsigned long _last_klax_ms = 0;
    unsigned long _last_ann_ms  = 0;
    bool     _klax_wait = false;   // cooldown, cleared in loop()
    bool     _ann_wait  = false;
    void     queueKlaxonOnAlert();
    void     queueAnnouncement();
    void     queueRadmonChange();
  public:
    uint8_t  voiceVolume() const { return _voice_vol; }
};

extern Voice voice;

#endif
#endif
