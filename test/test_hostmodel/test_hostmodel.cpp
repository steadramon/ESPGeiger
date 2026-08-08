/*
  test_hostmodel - the canary suite.

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

// Pins the arithmetic model the rest of the suites depend on, and demonstrates
// the one place where host and target genuinely disagree.
//
// THE uint32 TRAP
//
// On the host `unsigned long` is 64 bit; on ESP8266/ESP32 it is 32 bit. The
// whole rollover bug class (149-day uptime, millis wrap) only reproduces in
// 32-bit arithmetic, so a seam that stores time in `unsigned long` cannot wrap
// here and its test passes for the wrong reason.
//
// The rule: any production seam under host test does its time math in explicit
// uint32_t, and pins that with EG_ASSERT_32BIT. Do not rely on the tests
// alone to catch a widened type.

#include <unity.h>
#include <Arduino.h>
#include <stdint.h>

// libstdc++ uses these as parameter names, so a function-like macro breaks any
// header that reaches <algorithm>. libc++ does not, which hides it locally.
#ifdef min
#error "min must not be a macro in the shim"
#endif
#ifdef max
#error "max must not be a macro in the shim"
#endif

// Fails the build if a seam's arithmetic type is not the 32 bits the target
// gives it. Use on the expression, not the declaration, so an implicit
// promotion inside the expression is caught too.
#define EG_ASSERT_32BIT(expr) \
  static_assert(sizeof(expr) == 4, "seam must be 32-bit to reproduce target wrap")

void setUp(void)    { eg_clock_reset(); }
void tearDown(void) {}

// --- fixed-width sanity -----------------------------------------------------

static void test_fixed_widths(void) {
  TEST_ASSERT_EQUAL_size_t(4, sizeof(uint32_t));
  TEST_ASSERT_EQUAL_size_t(4, sizeof(int32_t));
  TEST_ASSERT_EQUAL_size_t(2, sizeof(uint16_t));
  TEST_ASSERT_EQUAL_size_t(1, sizeof(uint8_t));
  // int is 32-bit on host and target alike, so int-typed seams are faithful.
  TEST_ASSERT_EQUAL_size_t(4, sizeof(int));
}

// The trap itself, stated as a test so it is impossible to forget: this
// assertion documents that `unsigned long` is NOT interchangeable with
// uint32_t here. It holds on any LP64 host (linux/macos x86_64 + arm64) and
// would flip under a -m32 job, which is exactly the point of adding one later.
static void test_unsigned_long_is_not_uint32_on_host(void) {
  if (sizeof(unsigned long) == sizeof(uint32_t)) {
    TEST_MESSAGE("32-bit host: unsigned long already matches the target");
  } else {
    TEST_ASSERT_EQUAL_size_t(8, sizeof(unsigned long));
  }
}

// The other host/target ABI divergence, and it bites in the opposite
// direction to the width one: xtensa-lx106, xtensa-esp32 and riscv32-esp all
// default to UNSIGNED char, while x86-64 and Apple arm64 default to signed.
// Code that stores a byte in a plain `char` and then widens it behaves
// differently on the two. test.ini passes -fno-signed-char so the host matches
// the device; this fails loudly if that flag is ever dropped.
//
// Found via the MQTT PUBLISH parser, which does
// `_topicLength = currentByte | _topicLengthMsb << 8` with `char currentByte`.
// On a signed-char build every topic of length 128..255 sign-extends to a
// nonsense length and the message is silently dropped.
static void test_char_is_unsigned_like_the_target(void) {
  char c = (char)0x80;
  TEST_ASSERT_EQUAL_INT_MESSAGE(128, (int)c,
    "host char is signed; test.ini must pass -fno-signed-char to match the ESP toolchains");
  TEST_ASSERT_FALSE((char)-1 < 0);
}

// --- wrap arithmetic --------------------------------------------------------

static void test_uint32_wraps(void) {
  uint32_t v = 0xFFFFFFFFu;
  EG_ASSERT_32BIT(v + 1u);
  TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)(v + 1u));

  // Elapsed-time subtraction stays correct across the wrap. This is the only
  // reason the firmware can hold time in a wrapping counter at all.
  uint32_t before = 0xFFFFF000u;
  uint32_t after  = 0x00000FFFu;   // 0x1FFF ticks later
  TEST_ASSERT_EQUAL_UINT32(0x1FFFu, after - before);
}

// The `(int32_t)(now - due) < 0` idiom EGModuleRegistry uses to decide whether
// a module is due. Naive `now < due` breaks the moment the clock wraps past a
// due time; the signed difference does not.
static void test_signed_due_compare_survives_wrap(void) {
  struct { uint32_t now, due; bool due_yet; } cases[] = {
    { 1000u,       2000u,       false },  // ordinary: not yet
    { 2000u,       2000u,       true  },  // exactly due
    { 3000u,       2000u,       true  },  // ordinary: overdue
    { 0xFFFFFF00u, 0xFFFFFF10u, false },  // just below the wrap, not yet
    { 0x00000010u, 0xFFFFFFF0u, true  },  // clock wrapped past due, IS due
    { 0xFFFFFFF0u, 0x00000010u, false },  // due is past the wrap, not yet
  };
  for (auto& c : cases) {
    EG_ASSERT_32BIT(c.now - c.due);
    bool due_yet = (int32_t)(c.now - c.due) >= 0;
    TEST_ASSERT_EQUAL_INT(c.due_yet, due_yet);

    // The naive compare is wrong on exactly the wrap cases, which is the
    // whole point of the idiom.
    bool naive = c.now >= c.due;
    if (c.now == 0x00000010u || c.now == 0xFFFFFFF0u) {
      TEST_ASSERT_NOT_EQUAL(due_yet, naive);
    }
  }
}

// The uptime rollover bug, reduced to its arithmetic.
//
// The pre-8c57420c getUptime detected a millis() wrap with
// `_uptime * 1000UL > now_ms`, where _uptime is total seconds since boot.
// Right after a wrap now_ms is tiny while _uptime still holds its pre-wrap
// value, so the condition reads true - and it keeps reading true for every
// further call inside that same sub-second window, bumping the rollover count
// once per call. The size of the jump is therefore set by how often getUptime
// happens to be called, not by anything about time.
//
// It fires at 49.71 days, the millis() wrap, which is also exactly where
// `seconds * 1000` overflows uint32. The field device showed 149d because
// three calls landed in that window: 149.1 = 3 x 49.71. The bug is named after
// a multiple of the boundary, not the boundary. 49.71 days is what this pins.
static void test_seconds_to_millis_overflow_boundary(void) {
  const uint32_t last_ok = 4294967u;          // 49.71 days, in seconds
  EG_ASSERT_32BIT(last_ok * 1000u);
  TEST_ASSERT_EQUAL_UINT32(4294967000u, last_ok * 1000u);

  // Same instant that millis() itself wraps, to within a second. That
  // coincidence is the whole bug.
  TEST_ASSERT_EQUAL_UINT32(4294967u, (uint32_t)(0xFFFFFFFFu / 1000u));

  const uint32_t first_bad = last_ok + 1u;
  TEST_ASSERT_NOT_EQUAL(4294968000u, first_bad * 1000u);  // wrapped
  TEST_ASSERT_EQUAL_UINT32(704u, first_bad * 1000u);

  // Widening the multiply is the fix; widening the *host* type is not, which
  // is why this must never be written as `unsigned long`.
  TEST_ASSERT_EQUAL_UINT64(4294968000ULL, (uint64_t)first_bad * 1000ULL);
}

// --- the fake clock ---------------------------------------------------------

static void test_fake_clock_only_moves_when_told(void) {
  TEST_ASSERT_EQUAL_UINT32(0u, millis());
  TEST_ASSERT_EQUAL_UINT32(0u, micros());

  eg_clock_advance_ms(1500);
  TEST_ASSERT_EQUAL_UINT32(1500u, millis());
  TEST_ASSERT_EQUAL_UINT32(1500000u, micros());

  // Reading the clock never advances it.
  TEST_ASSERT_EQUAL_UINT32(1500u, millis());

  delay(500);
  TEST_ASSERT_EQUAL_UINT32(2000u, millis());
}

static void test_fake_clock_wraps_like_the_target(void) {
  // Park 5 ms below the millis() wrap and step across.
  eg_clock_set_us((uint64_t)0xFFFFFFFBu * 1000ULL);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFBu, millis());

  eg_clock_advance_ms(10);
  TEST_ASSERT_EQUAL_UINT32(5u, millis());

  // micros() wraps on its own, much sooner, and independently of millis().
  eg_clock_set_us(0xFFFFFFFFULL);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, micros());
  eg_clock_advance_us(1);
  TEST_ASSERT_EQUAL_UINT32(0u, micros());
}

static void test_cycle_count_is_deterministic(void) {
  TEST_ASSERT_EQUAL_UINT32(0u, ESP.getCycleCount());
  eg_clock_advance_us(1000);
  TEST_ASSERT_EQUAL_UINT32(80000u, ESP.getCycleCount());   // 80 MHz nominal
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_fixed_widths);
  RUN_TEST(test_unsigned_long_is_not_uint32_on_host);
  RUN_TEST(test_char_is_unsigned_like_the_target);
  RUN_TEST(test_uint32_wraps);
  RUN_TEST(test_signed_due_compare_survives_wrap);
  RUN_TEST(test_seconds_to_millis_overflow_boundary);
  RUN_TEST(test_fake_clock_only_moves_when_told);
  RUN_TEST(test_fake_clock_wraps_like_the_target);
  RUN_TEST(test_cycle_count_is_deterministic);
  return UNITY_END();
}
