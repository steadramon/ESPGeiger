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
#include "../Logger/Logger.h"

const char PinSafety::I2C[] PROGMEM = "I2C";

namespace {
  constexpr int MAX_CLAIMS = 16;
  struct PinClaim { int pin; const char* owner; };
  PinClaim s_claims[MAX_CLAIMS];
  int s_count = 0;

  const char SPI_FLASH[]    PROGMEM = "SPI flash";
  const char PSRAM[]        PROGMEM = "PSRAM";
  const char NO_SUCH[]      PROGMEM = "no such pin";
  const char USB[]          PROGMEM = "USB";
  const char INPUT_ONLY[]   PROGMEM = "input only";
  const char STRAP[]        PROGMEM = "boot strap, a pull the wrong way at power-on stops boot";
  const char NO_PULL[]      PROGMEM = "no internal pull-up, needs an external one";
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
// GPIO16 has no interrupt, GeigerInput refuses it for RX
const PinRule PinSafety::ESP8266_PINS[] PROGMEM = {
  {6, 11, PinSafety::PK_FLASH},
  {0, 0, PinSafety::PK_STRAP},     // low = download mode
  {2, 2, PinSafety::PK_STRAP},     // low stops boot
  {15, 15, PinSafety::PK_STRAP},   // high stops boot
  {16, 16, PinSafety::PK_NO_PULL},
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

// one row: a refusal, or nullptr and maybe a warning
static const char* row_says(PinSafety::PinKind kind, bool out, bool usb, const char** warn) {
  switch (kind) {
    case PinSafety::PK_FLASH:      return SPI_FLASH;
    case PinSafety::PK_PSRAM:      return PSRAM;
    case PinSafety::PK_MISSING:    return NO_SUCH;
    case PinSafety::PK_USB:        return usb ? USB : nullptr;
    case PinSafety::PK_INPUT_ONLY: return out ? INPUT_ONLY : nullptr;
    case PinSafety::PK_STRAP:      if (!out) *warn = STRAP; break;
    case PinSafety::PK_NO_PULL:    if (!out) *warn = NO_PULL; break;
  }
  return nullptr;
}

// rows can overlap, a refusal ends it, a warning carries on
const char* PinSafety::check(const PinRule* rows, int pin, bool out, bool usb, const char** warn) {
  *warn = nullptr;
  if (!rows) return nullptr;
  for (;; rows++) {
    PinRule r;
    memcpy_P(&r, rows, sizeof(r));
    if (r.lo == PIN_END) return nullptr;
    if (pin < r.lo || pin > r.hi) continue;
    if (const char* why = row_says(r.kind, out, usb, warn)) return why;
  }
}

const char* PinSafety::refuse(int pin, bool out) {
  if (pin == -1) return nullptr;
  if (pin < 0 || pin > MAX_GPIO_PIN) return OUT_OF_RANGE;
  const char* warn;
  return PinSafety::check(CHIP, pin, out, USB_CONSOLE, &warn);
}

static const char* claim_checked(int pin, const char* owner, bool out, bool button = false) {
  if (pin == -1) return nullptr;
  if (pin < 0 || pin > MAX_GPIO_PIN) return OUT_OF_RANGE;
  const char* warn;
  if (const char* why = PinSafety::check(CHIP, pin, out, USB_CONSOLE, &warn)) return why;
  if (button && warn == STRAP) warn = nullptr;   // open at power-on, like BOOT
  if (warn) Log::console(PSTR("PinSafety: pin=%d %s: %s"), pin, owner, warn);
  return PinSafety::claim(pin, owner);
}

const char* PinSafety::claim_output(int pin, const char* owner) {
  return claim_checked(pin, owner, true);
}

const char* PinSafety::claim_input(int pin, const char* owner) {
  return claim_checked(pin, owner, false);
}

const char* PinSafety::claim_button(int pin, const char* owner) {
  return claim_checked(pin, owner, false, true);
}

const char* PinSafety::claim(int pin, const char* owner) {
  if (pin == -1) return nullptr;
  for (int i = 0; i < s_count; i++) {
    if (s_claims[i].pin != pin) continue;
    if (same_owner(s_claims[i].owner, owner)) return nullptr;
    return s_claims[i].owner;
  }
  if (s_count < MAX_CLAIMS) {
    s_claims[s_count].pin = pin;
    s_claims[s_count].owner = owner;
    s_count++;
  } else {
    Log::console(PSTR("PinSafety: table full, pin=%d %s unchecked"), pin, owner);
  }
  return nullptr;
}
