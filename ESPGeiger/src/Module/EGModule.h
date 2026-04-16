/*
  EGModule.h - Base class for ESPGeiger modules

  Copyright (C) 2026 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/
#ifndef EGMODULE_H
#define EGMODULE_H

#include <Arduino.h>

// Lower priority values run first during begin() and tick().
// Suggested values:
//   10  - Hardware initialisation (HV, power, display, LEDs, etc.)
//   20  - Infrastructure (NTP, networking services)
//   50  - Default / standard modules (outputs, submissions)
//   90  - Modules that should run last
#define EG_PRIORITY_HARDWARE       10
#define EG_PRIORITY_INFRASTRUCTURE 20
#define EG_PRIORITY_DEFAULT        50
#define EG_PRIORITY_LATE           90

class EGModule {
  public:
    virtual ~EGModule() {}
    virtual const char* name() = 0;
    virtual uint8_t priority() { return EG_PRIORITY_DEFAULT; }
    virtual void pre_wifi() {}   // Called before WiFi autoConnect. No network available.
    virtual void begin() {}      // Called after WiFi autoConnect (or offline mode).
    virtual void loop(unsigned long now) {}
    virtual bool has_loop() { return false; }

    virtual uint16_t loop_interval_ms() { return 0; }
    virtual void s_tick(unsigned long now) {}

    virtual bool has_tick() { return false; }
    virtual bool requires_wifi() { return false; }
    virtual bool requires_ntp() { return false; }
    virtual uint16_t warmup_seconds() { return 10; }
};

#endif
