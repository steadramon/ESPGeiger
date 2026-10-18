/*
  test_respparse - HTTP response lines for AsyncHTTPRequest.

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

#include "../../lib/EGHttp/src/RespParse.h"

// writable copy, split is in place
static char buf[64];
static char* name;
static char* value;

static bool header(const char* line) {
  strcpy(buf, line);  // NOLINT(unsafe-call) test literals fit
  return eg_http_header(buf, strlen(line), &name, &value);
}

static int status(const char* line) {
  return eg_http_status(line, strlen(line));
}

static uint32_t size;

static bool chunk(const char* line) {
  size = 12345;
  return eg_http_chunk_size(line, strlen(line), &size);
}

void setUp(void) {
  name = value = NULL;
}

void tearDown(void) {}

static void status_with_reason(void) {
  TEST_ASSERT_EQUAL_INT(200, status("HTTP/1.1 200 OK"));
  TEST_ASSERT_EQUAL_INT(404, status("HTTP/1.0 404 Not Found"));
}

static void status_no_reason(void) {
  TEST_ASSERT_EQUAL_INT(204, status("HTTP/1.1 204"));
  TEST_ASSERT_EQUAL_INT(500, status("HTTP/1.1 500 "));
}

static void status_bounds(void) {
  TEST_ASSERT_EQUAL_INT(100, status("HTTP/1.1 100 Continue"));
  TEST_ASSERT_EQUAL_INT(599, status("HTTP/1.1 599 x"));
  TEST_ASSERT_EQUAL_INT(-1, status("HTTP/1.1 099 x"));
  TEST_ASSERT_EQUAL_INT(-1, status("HTTP/1.1 600 x"));
}

static void status_bad_shape(void) {
  TEST_ASSERT_EQUAL_INT(-1, status("HTTP/1.1 20 OK"));
  TEST_ASSERT_EQUAL_INT(-1, status("HTTP/1.1 2000 OK"));
  TEST_ASSERT_EQUAL_INT(-1, status("HTTP/1.1 2x0 OK"));
  TEST_ASSERT_EQUAL_INT(-1, status("HTTP/1.1  200 OK"));
  TEST_ASSERT_EQUAL_INT(-1, status("HTTP/1.x 200 OK"));
  TEST_ASSERT_EQUAL_INT(-1, status("HTTP/2.0 200 OK"));
  TEST_ASSERT_EQUAL_INT(-1, status("http/1.1 200 OK"));
  TEST_ASSERT_EQUAL_INT(-1, status("HTTP/1.1"));
  TEST_ASSERT_EQUAL_INT(-1, status(""));
}

static void status_ctrl(void) {
  TEST_ASSERT_EQUAL_INT(-1, status("HTTP/1.1 200 O\x01K"));
  TEST_ASSERT_EQUAL_INT(-1, eg_http_status("HTTP/1.1 200 O\0K", 16));
  TEST_ASSERT_EQUAL_INT(-1, eg_http_status(NULL, 12));
}

static void header_trim(void) {
  TEST_ASSERT_TRUE(header("Content-Length: 42"));
  TEST_ASSERT_EQUAL_STRING("Content-Length", name);
  TEST_ASSERT_EQUAL_STRING("42", value);
  TEST_ASSERT_TRUE(header("Connection:\t close \t"));
  TEST_ASSERT_EQUAL_STRING("close", value);
}

static void header_empty_value(void) {
  TEST_ASSERT_TRUE(header("X-Empty:"));
  TEST_ASSERT_EQUAL_STRING("X-Empty", name);
  TEST_ASSERT_EQUAL_STRING("", value);
  TEST_ASSERT_TRUE(header("X-Blank:   "));
  TEST_ASSERT_EQUAL_STRING("", value);
}

static void header_colons(void) {
  TEST_ASSERT_TRUE(header("Location: http://a:81/b"));
  TEST_ASSERT_EQUAL_STRING("Location", name);
  TEST_ASSERT_EQUAL_STRING("http://a:81/b", value);
}

static void header_bad_name(void) {
  TEST_ASSERT_FALSE(header("no colon here"));
  TEST_ASSERT_FALSE(header(": value"));
  TEST_ASSERT_FALSE(header("Name : value"));
  TEST_ASSERT_FALSE(header(" Folded: value"));
  TEST_ASSERT_FALSE(header("Na(me): value"));
}

static void header_ctrl(void) {
  TEST_ASSERT_FALSE(header("Name: va\x01lue"));
  TEST_ASSERT_FALSE(header("Name: va\x7Flue"));
  memcpy(buf, "Name: a\0b", 10);
  TEST_ASSERT_FALSE(eg_http_header(buf, 9, &name, &value));
}

static void header_outputs(void) {
  TEST_ASSERT_FALSE(header("bad line"));
  TEST_ASSERT_NULL(name);
  TEST_ASSERT_NULL(value);
}

static void chunk_size_hex(void) {
  TEST_ASSERT_TRUE(chunk("1a"));
  TEST_ASSERT_EQUAL_UINT32(26, size);
  TEST_ASSERT_TRUE(chunk("FF"));
  TEST_ASSERT_EQUAL_UINT32(255, size);
  TEST_ASSERT_TRUE(chunk("0"));
  TEST_ASSERT_EQUAL_UINT32(0, size);
}

static void chunk_size_ext(void) {
  TEST_ASSERT_TRUE(chunk("10;name=val"));
  TEST_ASSERT_EQUAL_UINT32(16, size);
  TEST_ASSERT_TRUE(chunk("10 ; name"));
  TEST_ASSERT_EQUAL_UINT32(16, size);
}

static void chunk_size_limit(void) {
  TEST_ASSERT_TRUE(chunk("ffffffff"));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, size);
  TEST_ASSERT_TRUE(chunk("0000000000000001"));
  TEST_ASSERT_EQUAL_UINT32(1, size);
  TEST_ASSERT_FALSE(chunk("100000000"));
}

static void chunk_size_bad(void) {
  TEST_ASSERT_FALSE(chunk(""));
  TEST_ASSERT_FALSE(chunk("x1"));
  TEST_ASSERT_FALSE(chunk(" 1"));
  TEST_ASSERT_FALSE(chunk("-1"));
  TEST_ASSERT_FALSE(chunk("1g"));
  TEST_ASSERT_FALSE(chunk("1 2"));
  TEST_ASSERT_FALSE(chunk("1\r"));
  TEST_ASSERT_FALSE(chunk("1;a\x01" "b"));
  TEST_ASSERT_EQUAL_UINT32(12345, size);
}

static bool tok(const char* s) {
  return eg_http_token(s, strlen(s));
}

static void token(void) {
  TEST_ASSERT_TRUE(tok("GET"));
  TEST_ASSERT_TRUE(tok("X-User-hash"));
  TEST_ASSERT_FALSE(tok(""));
  TEST_ASSERT_FALSE(tok("Bad:Name"));
  TEST_ASSERT_FALSE(tok("GE T"));
  TEST_ASSERT_FALSE(tok("X\r\nY"));
}

static void text(void) {
  TEST_ASSERT_TRUE(eg_http_text("a b\tc", 5));
  TEST_ASSERT_TRUE(eg_http_text("", 0));
  TEST_ASSERT_FALSE(eg_http_text("1\r\nEvil: 2", 10));
  TEST_ASSERT_FALSE(eg_http_text("a\x7f", 2));
}

// every byte, both signs of char
static void hex(void) {
  for (int i = 0; i < 256; i++) {
    char c = (char)i;
    int want = -1;
    if (i >= '0' && i <= '9') want = i - '0';
    if (i >= 'a' && i <= 'f') want = i - 'a' + 10;
    if (i >= 'A' && i <= 'F') want = i - 'A' + 10;
    TEST_ASSERT_EQUAL_INT(want, eg_http_hex(c));
    TEST_ASSERT_EQUAL_INT(want, eg_http_hex((char)(signed char)i));
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(hex);
  RUN_TEST(status_with_reason);
  RUN_TEST(status_no_reason);
  RUN_TEST(status_bounds);
  RUN_TEST(status_bad_shape);
  RUN_TEST(status_ctrl);
  RUN_TEST(header_trim);
  RUN_TEST(header_empty_value);
  RUN_TEST(header_colons);
  RUN_TEST(header_bad_name);
  RUN_TEST(header_ctrl);
  RUN_TEST(header_outputs);
  RUN_TEST(chunk_size_hex);
  RUN_TEST(chunk_size_ext);
  RUN_TEST(chunk_size_limit);
  RUN_TEST(chunk_size_bad);
  RUN_TEST(token);
  RUN_TEST(text);
  return UNITY_END();
}
