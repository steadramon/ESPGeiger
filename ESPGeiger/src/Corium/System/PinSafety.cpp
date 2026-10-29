/*
  PinSafety.cpp - Pin-in-use registry, one owner per pin.

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

#include "PinSafety.h"
#include "Corium/Logger/Logger.h"

const char PinSafety::I2C[] PROGMEM = "I2C";

namespace {
  constexpr int MAX_CLAIMS = PINSAFETY_MAX_CLAIMS;
  struct PinClaim { int16_t pin; bool fixed; const char* owner; };
  PinClaim s_claims[MAX_CLAIMS];
  int s_count = 0;
  char s_in_use[PinSafety::WHY_MAX];   // "in use: " + owner
  const PinSafety::Fixed* s_rows = nullptr;

  const char SPI_FLASH[]    PROGMEM = "SPI flash";
  const char PSRAM[]        PROGMEM = "PSRAM";
  const char NO_SUCH[]      PROGMEM = "no such pin";
  const char USB[]          PROGMEM = "USB";
  const char INPUT_ONLY[]   PROGMEM = "input only";
  const char STRAP[]        PROGMEM = "boot strap, a pull the wrong way at power-on stops boot";
  const char NO_PULL[]      PROGMEM = "no internal pull-up, needs an external one";
  const char NO_IRQ[]       PROGMEM = "no interrupt, edges are missed";
  const char NO_PWM[]       PROGMEM = "no PWM";
  const char NO_OD[]        PROGMEM = "no open-drain, I2C won't work";
  const char BOARD_TOO[]    PROGMEM = "the board uses it too";
  const char OUT_OF_RANGE[] PROGMEM = "out of range";

#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  constexpr bool USB_CONSOLE = true;
#else
  constexpr bool USB_CONSOLE = false;
#endif
}

using PinSafety::PinRule;
using PinSafety::PIN_END;

// https://www.espressif.com/sites/default/files/documentation/0a-esp8266ex_datasheet_en.pdf
// GPIO16 has no interrupt
const PinRule PinSafety::ESP8266_PINS[] PROGMEM = {
  {6, 11, PinSafety::PK_FLASH},
  {0, 0, PinSafety::PK_STRAP},     // low = download mode
  {2, 2, PinSafety::PK_STRAP},     // low stops boot
  {15, 15, PinSafety::PK_STRAP},   // high stops boot
  {16, 16, PinSafety::PK_NO_PULL},
  {16, 16, PinSafety::PK_NO_IRQ},
  {16, 16, PinSafety::PK_NO_PWM},
  {16, 16, PinSafety::PK_NO_OD},
  {PIN_END, 0, PinSafety::PK_FLASH},
};

// https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gpio.html
// 16-17 are PSRAM on WROVER, left open: UART2's default pins
const PinRule PinSafety::ESP32_PINS[] PROGMEM = {
  {6, 11, PinSafety::PK_FLASH},
  {20, 20, PinSafety::PK_MISSING},
  {24, 24, PinSafety::PK_MISSING},
  {28, 31, PinSafety::PK_MISSING},
  {34, 39, PinSafety::PK_INPUT_ONLY},
  {34, 39, PinSafety::PK_NO_PULL},
  {12, 12, PinSafety::PK_STRAP},   // high = 1.8 V flash
  {0, 0, PinSafety::PK_STRAP},     // low = download mode
  {2, 2, PinSafety::PK_STRAP},
  {5, 5, PinSafety::PK_STRAP},
  {15, 15, PinSafety::PK_STRAP},
  {PIN_END, 0, PinSafety::PK_FLASH},
};

// https://docs.espressif.com/projects/esp-idf/en/stable/esp32s2/api-reference/peripherals/gpio.html
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32s2/api-guides/usb-otg-console.html
const PinRule PinSafety::ESP32S2_PINS[] PROGMEM = {
  {22, 25, PinSafety::PK_MISSING},
  {26, 32, PinSafety::PK_FLASH},
  {19, 20, PinSafety::PK_USB},
  {46, 46, PinSafety::PK_INPUT_ONLY},
  {45, 45, PinSafety::PK_STRAP},   // flash voltage
  {0, 0, PinSafety::PK_STRAP},     // low = download mode
  {46, 46, PinSafety::PK_STRAP},
  {PIN_END, 0, PinSafety::PK_FLASH},
};

// https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gpio.html
// 33-37 only on octal PSRAM chips (S3R8), refused on all: can't tell at build
const PinRule PinSafety::ESP32S3_PINS[] PROGMEM = {
  {22, 25, PinSafety::PK_MISSING},
  {26, 32, PinSafety::PK_FLASH},
  {33, 37, PinSafety::PK_PSRAM},
  {19, 20, PinSafety::PK_USB},
  {45, 45, PinSafety::PK_STRAP},   // flash voltage
  {0, 0, PinSafety::PK_STRAP},     // low = download mode
  {3, 3, PinSafety::PK_STRAP},
  {46, 46, PinSafety::PK_STRAP},
  {PIN_END, 0, PinSafety::PK_FLASH},
};

// https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/peripherals/gpio.html
// 11 is VDD_SPI, powers the flash unless an efuse says not
const PinRule PinSafety::ESP32C3_PINS[] PROGMEM = {
  {11, 17, PinSafety::PK_FLASH},
  {18, 19, PinSafety::PK_USB},
  {2, 2, PinSafety::PK_STRAP},
  {8, 8, PinSafety::PK_STRAP},
  {9, 9, PinSafety::PK_STRAP},     // low = download mode
  {PIN_END, 0, PinSafety::PK_FLASH},
};

// https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/gpio.html
const PinRule PinSafety::ESP32C6_PINS[] PROGMEM = {
  {24, 30, PinSafety::PK_FLASH},
  {12, 13, PinSafety::PK_USB},
  {PIN_END, 0, PinSafety::PK_FLASH},
};

namespace {
#if defined(ESP8266)
  const PinRule* const CHIP = PinSafety::ESP8266_PINS;
#elif defined(CONFIG_IDF_TARGET_ESP32)
  const PinRule* const CHIP = PinSafety::ESP32_PINS;
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
  const PinRule* const CHIP = PinSafety::ESP32S2_PINS;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  const PinRule* const CHIP = PinSafety::ESP32S3_PINS;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  const PinRule* const CHIP = PinSafety::ESP32C3_PINS;
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
  const PinRule* const CHIP = PinSafety::ESP32C6_PINS;
#elif defined(PINSAFETY_TEST_CHIP)
  const PinRule* const CHIP = PINSAFETY_TEST_CHIP;
#else
  const PinRule* const CHIP = nullptr;   // host
#endif

  // both PROGMEM
  bool same_owner(const char* a, const char* b) {
    if (a == b) return true;
    for (;; a++, b++) {
      char c = pgm_read_byte(a);
      if (c != (char)pgm_read_byte(b)) return false;
      if (!c) return true;
    }
  }
}

// when a kind speaks, past the Need bits
enum : uint8_t { IF_OUT = 8, IF_IN = 16, IF_USB = 32, ALWAYS = 64 };

// by PinKind, up to PK_INPUT_ONLY a refusal, after a warning
static const char* const KIND_SAYS[] PROGMEM = {
  SPI_FLASH, PSRAM, NO_SUCH, USB, INPUT_ONLY, STRAP, NO_PULL, NO_IRQ, NO_PWM, NO_OD,
};
static const uint8_t KIND_WHEN[] PROGMEM = {
  ALWAYS, ALWAYS, ALWAYS, IF_USB, IF_OUT, IF_IN, IF_IN,
  PinSafety::NEED_IRQ, PinSafety::NEED_PWM, PinSafety::NEED_OD,
};
static_assert(sizeof(KIND_WHEN) == PinSafety::PK_NO_OD + 1, "a KIND_WHEN per PinKind");

// rows can overlap, a refusal ends it, a warning carries on
const char* PinSafety::check(const PinRule* rows, int pin, bool out, bool usb, const char** warn,
                             uint8_t needs) {
  *warn = nullptr;
  if (!rows) return nullptr;
  uint8_t now = needs | (out ? IF_OUT : IF_IN) | (usb ? IF_USB : 0) | ALWAYS;
  for (;; rows++) {
    PinRule r;
    memcpy_P(&r, rows, sizeof(r));
    if (r.lo == PIN_END) return nullptr;
    if (pin < r.lo || pin > r.hi) continue;
    if (!(pgm_read_byte(&KIND_WHEN[r.kind]) & now)) continue;
    const char* says = (const char*)pgm_read_ptr(&KIND_SAYS[r.kind]);
    if (r.kind <= PK_INPUT_ONLY) return says;
    *warn = says;
  }
}

static PinClaim* find(int pin) {
  for (int i = 0; i < s_count; i++) {
    if (s_claims[i].pin == pin) return &s_claims[i];
  }
  return nullptr;
}

// owner of the board's shared row on it, else nullptr
static const char* shared_with(int pin) {
  for (const PinSafety::Fixed* rows = s_rows; rows; rows++) {
    PinSafety::Fixed r;
    memcpy_P(&r, rows, sizeof(r));
    if (!r.owner) return nullptr;
    if (r.pin == pin) return r.owner;
  }
  return nullptr;
}

// boot and save both: a button is open at power-on, like BOOT
static const char* checked(int pin, PinSafety::Use use, uint8_t needs, const char** warn) {
  *warn = nullptr;
  if (pin == -1) return nullptr;
  if (pin < 0 || pin > MAX_GPIO_PIN) return OUT_OF_RANGE;
  bool out = use == PinSafety::USE_OUT;
  if (const char* why = PinSafety::check(CHIP, pin, out, USB_CONSOLE, warn, needs)) return why;
  if (use == PinSafety::USE_BUTTON && *warn == STRAP) *warn = nullptr;
  return nullptr;
}

const char* PinSafety::refuse(int pin, Use use, uint8_t needs, const char** warn) {
  if (const char* why = checked(pin, use, needs, warn)) return why;
  PinClaim* c = find(pin);
  if (!c || !c->fixed) {
    if (!*warn && shared_with(pin)) *warn = BOARD_TOO;
    return nullptr;
  }
  snprintf_P(s_in_use, sizeof(s_in_use), PSTR("in use: %s"), c->owner);
  return s_in_use;
}

static const char* claim_checked(int pin, const char* owner, PinSafety::Use use) {
  const char* warn;
  if (const char* why = checked(pin, use, 0, &warn)) return why;
  if (warn) Log::console(PSTR("PinSafety: pin=%d %s: %s"), pin, owner, warn);
  return PinSafety::claim(pin, owner);
}

const char* PinSafety::claim_output(int pin, const char* owner) {
  return claim_checked(pin, owner, USE_OUT);
}

const char* PinSafety::claim_input(int pin, const char* owner) {
  return claim_checked(pin, owner, USE_IN);
}

const char* PinSafety::claim_button(int pin, const char* owner) {
  return claim_checked(pin, owner, USE_BUTTON);
}

const char* PinSafety::claim(int pin, const char* owner) {
  if (pin == -1) return nullptr;
  if (PinClaim* c = find(pin)) return same_owner(c->owner, owner) ? nullptr : c->owner;
  const char* with = shared_with(pin);
  if (with && !same_owner(with, owner)) {
    Log::console(PSTR("PinSafety: pin=%d %s, shared with %s"), pin, owner, with);
  }
  if (s_count < MAX_CLAIMS) {
    s_claims[s_count].pin = (int16_t)pin;
    s_claims[s_count].fixed = false;
    s_claims[s_count].owner = owner;
    s_count++;
  } else {
    Log::console(PSTR("PinSafety: table full, pin=%d %s unchecked"), pin, owner);
  }
  return nullptr;
}

void PinSafety::share(const Fixed* rows) { s_rows = rows; }

void PinSafety::reserve(const Fixed* rows) {
  for (;; rows++) {
    Fixed r;
    memcpy_P(&r, rows, sizeof(r));
    if (!r.owner) return;
    // the module logs any warning when it claims
    const char* warn;
    const char* why = checked(r.pin, r.use, 0, &warn);
    if (!why) why = claim(r.pin, r.owner);
    if (why) {
      Log::console(PSTR("PinSafety: pin=%d %s refused (%s)"), r.pin, r.owner, why);
      continue;
    }
    if (PinClaim* c = find(r.pin)) c->fixed = true;
  }
}
