/*
  PushButton.h - User interaction via button

  Copyright (C) 2023 @steadramon

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
#ifdef GEIGER_PUSHBUTTON
#ifndef PUSHBUTTON_H
#define PUSHBUTTON_H
#include <Arduino.h>
#include "../Status.h"
#include "../Counter/Counter.h"
#include "../Module/EGModule.h"

#ifndef PUSHBUTTON_PIN
#define PUSHBUTTON_PIN 14
#endif

extern Status status;
extern Counter gcounter;

class PushButton : public EGModule {
  public:
    PushButton();
    const char* name() override { return "button"; }
    uint8_t priority() override { return EG_PRIORITY_HARDWARE; }
    uint16_t warmup_seconds() override { return 0; }
    void loop(unsigned long now) override;
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return 10; }
    void begin() override;
    void init();
    void read();
    bool isPressed();
};

extern PushButton pushbutton;

#endif
#endif