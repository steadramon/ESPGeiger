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

// The server sizes its body buffer and body scan from this one number.

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

static void test_absent_is_zero(void) {
  size_t v = 99;
  TEST_ASSERT_EQUAL(EGHTTP_CL_ABSENT, run(req(nullptr), &v));
  TEST_ASSERT_EQUAL_UINT32(0, v);
}

static void test_plain_value(void) {
  size_t v = 0;
  TEST_ASSERT_EQUAL(EGHTTP_CL_OK, run(req("Content-Length: 42"), &v));
  TEST_ASSERT_EQUAL_UINT32(42, v);
}

static void test_case_and_spacing(void) {
  size_t v = 0;
  TEST_ASSERT_EQUAL(EGHTTP_CL_OK, run(req("content-length:7"), &v));
  TEST_ASSERT_EQUAL_UINT32(7, v);
  TEST_ASSERT_EQUAL(EGHTTP_CL_OK, run(req("CONTENT-LENGTH:   7  "), &v));
  TEST_ASSERT_EQUAL_UINT32(7, v);
}

static void test_zero(void) {
  size_t v = 5;
  TEST_ASSERT_EQUAL(EGHTTP_CL_OK, run(req("Content-Length: 0"), &v));
  TEST_ASSERT_EQUAL_UINT32(0, v);
}

static void test_eight_digits_ok_nine_bad(void) {
  size_t v = 0;
  TEST_ASSERT_EQUAL(EGHTTP_CL_OK, run(req("Content-Length: 99999999"), &v));
  TEST_ASSERT_EQUAL_UINT32(99999999u, v);
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, run(req("Content-Length: 100000000"), &v));
}

// -1 through atol is a 4 GB body.

static void test_negative_is_bad(void) {
  size_t v = 0;
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, run(req("Content-Length: -1"), &v));
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, run(req("Content-Length: +5"), &v));
}

static void test_junk_is_bad(void) {
  size_t v = 0;
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, run(req("Content-Length: 12abc"), &v));
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, run(req("Content-Length: abc"), &v));
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, run(req("Content-Length:"), &v));
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, run(req("Content-Length: 1 2"), &v));
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, run(req("Content-Length: 0x10"), &v));
}

// The scan is bounded by headerEnd, never by a NUL.
static void test_never_reads_past_header_end(void) {
  std::string r = "GET / HTTP/1.1\r\nContent-Length: 12";
  std::string tail = "34\r\n\r\n";
  std::string all = r + tail;
  size_t v = 0;
  EGHttpCLResult res = eghttp_content_length(all.data(), r.size(), &v);
  TEST_ASSERT_EQUAL(EGHTTP_CL_BAD, res);
}

static void test_first_header_wins(void) {
  size_t v = 0;
  std::string r = "POST / HTTP/1.1\r\nContent-Length: 3\r\nContent-Length: 999\r\n\r\n";
  TEST_ASSERT_EQUAL(EGHTTP_CL_OK, run(r, &v));
  TEST_ASSERT_EQUAL_UINT32(3, v);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_absent_is_zero);
  RUN_TEST(test_plain_value);
  RUN_TEST(test_case_and_spacing);
  RUN_TEST(test_zero);
  RUN_TEST(test_eight_digits_ok_nine_bad);
  RUN_TEST(test_negative_is_bad);
  RUN_TEST(test_junk_is_bad);
  RUN_TEST(test_never_reads_past_header_end);
  RUN_TEST(test_first_header_wins);
  return UNITY_END();
}
