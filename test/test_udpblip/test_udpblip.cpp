/*
  test_udpblip - UdpBlip OSC packet bytes.

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
#include <unity.h>
#include <string.h>

#include "UdpBlip/BlipPacket.h"

void setUp(void)    {}
void tearDown(void) {}

static void click() {
  static const uint8_t want[] = {
    '/','e','s','p','g','/','c','0','f','f','e','e','/','c','l','i','c','k', 0,0,
    ',','i','i', 0,
    0,0,0,0,
    0,0,0,0,
  };
  uint8_t buf[BLIP_CLICK_LEN];
  TEST_ASSERT_EQUAL(sizeof(want), BLIP_CLICK_LEN);
  TEST_ASSERT_EQUAL(BLIP_CLICK_LEN, blip_click(buf, sizeof(buf), "/espg/c0ffee/click"));
  TEST_ASSERT_EQUAL_MEMORY(want, buf, sizeof(want));
  TEST_ASSERT_EQUAL_MEMORY("c0ffee", buf + BLIP_ID_AT, BLIP_ID_LEN);
  TEST_ASSERT_EQUAL_MEMORY("click", buf + BLIP_SUFFIX_AT, 5);
  TEST_ASSERT_EQUAL_MEMORY(",ii", buf + BLIP_ARGS_AT, 3);
  TEST_ASSERT_EQUAL(BLIP_ARGS_AT + 4, BLIP_CLICK_COUNT_AT);
  TEST_ASSERT_EQUAL(BLIP_CLICK_COUNT_AT + 4, BLIP_CLICK_MS_AT);
}

static void rad() {
  static const uint8_t want[] = {
    '/','e','s','p','g','/','c','0','f','f','e','e','/','r','a','d', 0,0,0,0,
    ',','f','f','s','i', 0,0,0,
    0x41,0xC4,0x00,0x00,
    0x3E,0x80,0x00,0x00,
    'h','e','a','l','t','h','y', 0,
    0x01,0x02,0x03,0x04,
  };
  uint8_t buf[96];
  size_t n = blip_rad(buf, sizeof(buf), "/espg/c0ffee/rad", { 24.5f, 0.25f, "healthy", 0x01020304 });
  TEST_ASSERT_EQUAL(sizeof(want), n);
  TEST_ASSERT_EQUAL_MEMORY(want, buf, n);
}

static void rad_full() {
  uint8_t buf[47];
  TEST_ASSERT_EQUAL(0, blip_rad(buf, sizeof(buf), "/espg/c0ffee/rad", { 24.5f, 0.25f, "healthy", 1 }));
}

static void hv() {
  static const uint8_t want[] = {
    '/','e','s','p','g','/','c','0','f','f','e','e','/','h','v', 0,
    ',','f','f','i','i', 0,0,0,
    0x43,0xC8,0x00,0x00,
    0x43,0xC8,0x00,0x00,
    0x00,0x00,0x00,0x46,
    0xFF,0xFF,0xFF,0xFD,
  };
  uint8_t buf[64];
  size_t n = blip_hv(buf, sizeof(buf), "/espg/c0ffee/hv", { 400.0f, 400.0f, 70, -3 });
  TEST_ASSERT_EQUAL(sizeof(want), n);
  TEST_ASSERT_EQUAL_MEMORY(want, buf, n);
}

static void env() {
  static const uint8_t want[] = {
    '/','e','s','p','g','/','c','0','f','f','e','e','/','e','n','v', 0,0,0,0,
    ',','f','f','f', 0,0,0,0,
    0x41,0xAC,0x00,0x00,
    0x42,0x48,0x00,0x00,
    0x44,0x7D,0x50,0x00,
  };
  uint8_t buf[64];
  size_t n = blip_env(buf, sizeof(buf), "/espg/c0ffee/env", 21.5f, 50.0f, 1013.25f);
  TEST_ASSERT_EQUAL(sizeof(want), n);
  TEST_ASSERT_EQUAL_MEMORY(want, buf, n);
  TEST_ASSERT_EQUAL(BLIP_ENV_LEN, n);
  TEST_ASSERT_EQUAL_MEMORY("env", buf + BLIP_SUFFIX_AT, 4);
  TEST_ASSERT_EQUAL_MEMORY(",fff", buf + BLIP_ARGS_AT, 4);
  TEST_ASSERT_EQUAL(0x41, buf[BLIP_ENV_AT]);
}

static void sys() {
  static const uint8_t want[] = {
    '/','e','s','p','g','/','c','0','f','f','e','e','/','s','y','s', 0,0,0,0,
    ',','i','i','i','i', 0,0,0,
    0x00,0x00,0x0E,0x10,
    0xFF,0xFF,0xFF,0xC4,
    0x00,0x01,0xB0,0xE4,
    0x00,0x00,0x05,0xDC,
  };
  uint8_t buf[64];
  size_t n = blip_sys(buf, sizeof(buf), "/espg/c0ffee/sys", { 3600, -60, 110820, 1500 });
  TEST_ASSERT_EQUAL(sizeof(want), n);
  TEST_ASSERT_EQUAL_MEMORY(want, buf, n);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(click);
  RUN_TEST(rad);
  RUN_TEST(rad_full);
  RUN_TEST(hv);
  RUN_TEST(env);
  RUN_TEST(sys);
  return UNITY_END();
}
