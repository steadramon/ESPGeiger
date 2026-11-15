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
#include <stdint.h>
#include "Corium/Platform/Flash.h"
#include "App/AppConfig.h"
#include "Corium/Module/Module.h"
#include "Corium/Module/Registry.h"
#include "App/Topics.h"
#include "Corium/Prefs/Prefs.h"

#ifndef PUSHBUTTON_PIN
  #ifdef ESP32
  #define PUSHBUTTON_PIN 0   // BOOT button
  #else
  #define PUSHBUTTON_PIN -1
  #endif
#endif
#ifndef PUSHBUTTON_PIN2
#define PUSHBUTTON_PIN2 -1
#endif

// One or two buttons, active low, presses posted as topics
class PushButton : public corium::Module {
  public:
    PushButton();
    const char* name() override { return PSTR("btn"); }
    uint8_t priority() override { return CORIUM_PRIORITY_HARDWARE; }
    void loop(unsigned long now) override;
    // from loop(); a long only when something hears it
    static constexpr uint8_t TOPIC_PRESS = Topic::PRESS;
    static constexpr uint8_t TOPIC_BACK  = Topic::BACK;
    static constexpr uint8_t TOPIC_LONG  = Topic::LONG;
    void begin() override;
    void init();
    bool isPressed();
    bool is_active();
    void set_pin(int pin);
    void set_pin2(int pin);
#ifndef BTN_PIN_BLOCKED
    const corium::PrefGroup* prefsGroup() override;
    void onPrefsLoaded() override;
#endif
};

extern PushButton pushbutton;

#endif
#endif
