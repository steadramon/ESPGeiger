/*
  test_urlparse - splitting a URL for AsyncHTTPRequest.

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

#include "../../lib/EGHttp/src/UrlParse.h"

static EGUrlParts p;

void setUp(void) {
  memset(&p, 0, sizeof(p));
}

void tearDown(void) {}

static void assert_part(const char* what, const char* got, size_t len, const char* want) {
  TEST_ASSERT_EQUAL_size_t_MESSAGE(strlen(want), len, what);
  if (len)
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want, got, len, what);
}

static void scheme_host_path(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com/a/b", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("path", p.path, p.path_len, "/a/b");
  assert_part("query", p.query, p.query_len, "");
  TEST_ASSERT_EQUAL_INT(80, p.port);
}

static void scheme_case(void) {
  TEST_ASSERT_TRUE(eg_parse_url("HtTp://example.com/a", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("path", p.path, p.path_len, "/a");
}

static void no_scheme(void) {
  TEST_ASSERT_TRUE(eg_parse_url("example.com/a", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("path", p.path, p.path_len, "/a");
}

static void https_rejected(void) {
  TEST_ASSERT_FALSE(eg_parse_url("https://example.com/a", &p));
}

static void port_before_path(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com:8080/a", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("path", p.path, p.path_len, "/a");
  TEST_ASSERT_EQUAL_INT(8080, p.port);
}

static void port_without_path(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com:8080", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("path", p.path, p.path_len, "/");
  TEST_ASSERT_EQUAL_INT(8080, p.port);
}

static void port_before_query(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com:8080?a=1", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("query", p.query, p.query_len, "?a=1");
  TEST_ASSERT_EQUAL_INT(8080, p.port);
}

static void query_colon(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com?t=1:2", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("query", p.query, p.query_len, "?t=1:2");
  TEST_ASSERT_EQUAL_INT(80, p.port);
}

static void query_no_path(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com?a=1&b=2", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("path", p.path, p.path_len, "/");
  assert_part("query", p.query, p.query_len, "?a=1&b=2");
}

static void query_slash(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com?url=a/b", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("path", p.path, p.path_len, "/");
  assert_part("query", p.query, p.query_len, "?url=a/b");
}

static void path_and_query(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com/a?b=1", &p));
  assert_part("path", p.path, p.path_len, "/a");
  assert_part("query", p.query, p.query_len, "?b=1");
}

static void bare_host(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("path", p.path, p.path_len, "/");
  assert_part("query", p.query, p.query_len, "");
}

static void empty_host(void) {
  TEST_ASSERT_FALSE(eg_parse_url("", &p));
  TEST_ASSERT_FALSE(eg_parse_url("http://", &p));
  TEST_ASSERT_FALSE(eg_parse_url("http://:80/a", &p));
}

static void truncated_scheme(void) {
  TEST_ASSERT_TRUE(eg_parse_url("htt", &p));
  assert_part("host", p.host, p.host_len, "htt");
}

static void null_url(void) {
  TEST_ASSERT_FALSE(eg_parse_url(NULL, &p));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(scheme_host_path);
  RUN_TEST(scheme_case);
  RUN_TEST(no_scheme);
  RUN_TEST(https_rejected);
  RUN_TEST(port_before_path);
  RUN_TEST(port_without_path);
  RUN_TEST(port_before_query);
  RUN_TEST(query_colon);
  RUN_TEST(query_no_path);
  RUN_TEST(query_slash);
  RUN_TEST(path_and_query);
  RUN_TEST(bare_host);
  RUN_TEST(empty_host);
  RUN_TEST(truncated_scheme);
  RUN_TEST(null_url);
  return UNITY_END();
}
