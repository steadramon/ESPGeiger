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
    bool has_loop() override { return false; }
    void begin() override;
    void registerRoutes(EGHttpServer& http) override;

    void speakDigit(uint8_t d, bool rising);
    void speakDigits(const char* s);   // plays each '0'-'9' in s, rising on last
    void say(const char* text);        // ADPCM only speaks digits - any 0-9 in text plays
    void silence(uint16_t ms);
};

extern Voice voice;

#endif
#endif
