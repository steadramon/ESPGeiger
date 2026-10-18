/*
  test_eghttp_headers - the request header recognisers.

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
#include <string>

#include "../../lib/EGHttp/src/EGHttpHeaders.h"

void setUp(void)    {}
void tearDown(void) {}

static std::string req(const char* cl_line) {
  std::string r = "POST /param HTTP/1.1\r\nHost: x\r\n";
  if (cl_line) { r += cl_line; r += "\r\n"; }
  r += "\r\n";
  return r;
}

static EGHttpCLResult run(const std::string& r, size_t* out) {
  return eghttp_content_length(r.data(), r.size(), out);
}

static void absent_is_zero(void) {
  size_t v = 99;
  TEST_ASSERT_EQUAL(EGHTTP_CL_ABSENT, run(req(nullptr), &v));
  TEST_ASSERT_EQUAL_UINT32(0, v);
}

static void plain_value(void) {
  size_t v = 0;
  TEST_ASSERT_EQUAL(EGHTTP_CL_OK, run(req("Content-Length: 42"), &v));
  TEST_ASSERT_EQUAL_UINT32(42, v);
}

static void case_and_spacing(void) {
  size_t v = 0;
  TEST_ASSERT_EQUAL(EGHTTP_CL_OK, run(req("content-length:7"), &v));
  TEST_ASSERT_EQUAL_UINT32(7, v);
  TEST_ASSERT_EQUAL(EGHTTP_CL_OK, run(req("CONTENT-LENGTH:   7  "), &v));
  TEST_ASSERT_EQUAL_UINT32(7, v);
}

static void zero_length(void) {
  size_t v = 5;
  TEST_ASSERT_EQUAL(EGHTTP_CL_OK, run(req("Content-Length: 0"), &v));
  TEST_ASSERT_EQUAL_UINT32(0, v);
}

static void nine_digits(void) {
  size_t v = 0;
  TEST_ASSERT_EQUAL(EGHTTP_CL_OK, run(req("Content-Length: 99999999"), &v));
  TEST_ASSERT_EQUAL_UINT32(99999999u, v);
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, run(req("Content-Length: 100000000"), &v));
}

// -1 via atol is 4 GB

static void negative_is_bad(void) {
  size_t v = 0;
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, run(req("Content-Length: -1"), &v));
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, run(req("Content-Length: +5"), &v));
}

static void junk_is_bad(void) {
  size_t v = 0;
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, run(req("Content-Length: 12abc"), &v));
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, run(req("Content-Length: abc"), &v));
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, run(req("Content-Length:"), &v));
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, run(req("Content-Length: 1 2"), &v));
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, run(req("Content-Length: 0x10"), &v));
}

static void header_end(void) {
  std::string r = "GET / HTTP/1.1\r\nContent-Length: 12";
  std::string tail = "34\r\n\r\n";
  std::string all = r + tail;
  size_t v = 0;
  EGHttpCLResult res = eghttp_content_length(all.data(), r.size(), &v);
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, res);
}

static void first_header_wins(void) {
  size_t v = 0;
  std::string r = "POST / HTTP/1.1\r\nContent-Length: 3\r\nContent-Length: 999\r\n\r\n";
  TEST_ASSERT_EQUAL(EGHTTP_CL_OK, run(r, &v));
  TEST_ASSERT_EQUAL_UINT32(3, v);
}

static void line_start(void) {
  size_t v = 0;
  TEST_ASSERT_EQUAL(EGHTTP_CL_ABSENT, run(req("X-Content-Length: 99"), &v));
  std::string r = "POST / HTTP/1.1\r\nX-Content-Length: 99\r\nX-Note: Content-Length: 98\r\n"
                  "Content-Length: 4\r\n\r\n";
  TEST_ASSERT_EQUAL(EGHTTP_CL_OK, run(r, &v));
  TEST_ASSERT_EQUAL_UINT32(4, v);
}

static const char* find(const std::string& r, size_t end, const char* name) {
  return eghttp_find_header(r.data(), end, name);
}

static void find_header(void) {
  std::string r = "GET / HTTP/1.1\r\nX-Auth: a\r\nauth: b\r\nAuth: c\r\n\r\n";
  const char* p = find(r, r.size(), "Auth:");
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQUAL_INT(0, strncmp(p, " b\r\n", 4));
  TEST_ASSERT_NULL(find(r, r.size(), "Host:"));
  TEST_ASSERT_NULL(find(r, 0, "Auth:"));
}

// a value and its CR past the colon
static void find_header_end(void) {
  std::string r = "GET / HTTP/1.1\r\nAuth: b\r\n\r\n";
  size_t colon = r.find(':') + 1;
  TEST_ASSERT_NULL(find(r, colon, "Auth:"));
  TEST_ASSERT_NULL(find(r, colon + 1, "Auth:"));
  TEST_ASSERT_EQUAL_PTR(r.data() + colon, find(r, colon + 2, "Auth:"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(find_header);
  RUN_TEST(find_header_end);
  RUN_TEST(line_start);
  RUN_TEST(absent_is_zero);
  RUN_TEST(plain_value);
  RUN_TEST(case_and_spacing);
  RUN_TEST(zero_length);
  RUN_TEST(nine_digits);
  RUN_TEST(negative_is_bad);
  RUN_TEST(junk_is_bad);
  RUN_TEST(header_end);
  RUN_TEST(first_header_wins);
  return UNITY_END();
}
