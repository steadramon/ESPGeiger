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

// The caller sizes an allocation from each length here and copies that many
// bytes, so a length that disagrees with its pointer is a heap overflow. The
// original String version got this wrong for a url with a query and no path,
// copying the query into a buffer sized for the path. Reachable from a
// webhook.url of the shape host?query.

#include <unity.h>
#include <string.h>

#include "../../lib/AsyncHTTPRequest_Generic/src/UrlParse.h"

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

static void test_scheme_host_and_path(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com/a/b", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("path", p.path, p.path_len, "/a/b");
  assert_part("query", p.query, p.query_len, "");
  TEST_ASSERT_EQUAL_INT(80, p.port);
}

static void test_scheme_match_is_case_insensitive(void) {
  TEST_ASSERT_TRUE(eg_parse_url("HtTp://example.com/a", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("path", p.path, p.path_len, "/a");
}

static void test_a_url_with_no_scheme_parses(void) {
  TEST_ASSERT_TRUE(eg_parse_url("example.com/a", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("path", p.path, p.path_len, "/a");
}

static void test_https_is_rejected(void) {
  TEST_ASSERT_FALSE(eg_parse_url("https://example.com/a", &p));
}

static void test_a_port_before_a_path_is_taken(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com:8080/a", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("path", p.path, p.path_len, "/a");
  TEST_ASSERT_EQUAL_INT(8080, p.port);
}

// A port needs no path after it. The String version only read one when a '/'
// followed, so this shape silently stayed on 80.
static void test_a_port_with_no_path_is_taken(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com:8080", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("path", p.path, p.path_len, "/");
  TEST_ASSERT_EQUAL_INT(8080, p.port);
}

static void test_a_port_before_a_query_is_taken(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com:8080?a=1", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("query", p.query, p.query_len, "?a=1");
  TEST_ASSERT_EQUAL_INT(8080, p.port);
}

// A colon past the '?' belongs to the query, not to the host.
static void test_a_colon_inside_the_query_is_not_a_port(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com?t=1:2", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("query", p.query, p.query_len, "?t=1:2");
  TEST_ASSERT_EQUAL_INT(80, p.port);
}

// path is the literal "/", not a view sized from a negative pathBeg. That is
// the overrun the String version had. The host stops at the '?', which it did
// not: it resolved a hostname of "example.com?a=1&b=2".
static void test_a_query_with_no_path_yields_a_root_path(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com?a=1&b=2", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("path", p.path, p.path_len, "/");
  assert_part("query", p.query, p.query_len, "?a=1&b=2");
}

// The sibling: the only '/' sits inside the query, so pathBeg is past queryBeg
// and the path length would go negative.
static void test_a_slash_inside_the_query_is_not_a_path(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com?url=a/b", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("path", p.path, p.path_len, "/");
  assert_part("query", p.query, p.query_len, "?url=a/b");
}

static void test_path_and_query_split_at_the_question_mark(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com/a?b=1", &p));
  assert_part("path", p.path, p.path_len, "/a");
  assert_part("query", p.query, p.query_len, "?b=1");
}

static void test_bare_host_yields_a_root_path(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://example.com", &p));
  assert_part("host", p.host, p.host_len, "example.com");
  assert_part("path", p.path, p.path_len, "/");
  assert_part("query", p.query, p.query_len, "");
}

static void test_an_empty_url_parses_to_empty_parts(void) {
  TEST_ASSERT_TRUE(eg_parse_url("", &p));
  assert_part("host", p.host, p.host_len, "");
  assert_part("path", p.path, p.path_len, "/");
  assert_part("query", p.query, p.query_len, "");
}

static void test_a_scheme_with_nothing_after_it_parses(void) {
  TEST_ASSERT_TRUE(eg_parse_url("http://", &p));
  assert_part("host", p.host, p.host_len, "");
  assert_part("path", p.path, p.path_len, "/");
}

static void test_a_truncated_scheme_is_treated_as_a_host(void) {
  TEST_ASSERT_TRUE(eg_parse_url("htt", &p));
  assert_part("host", p.host, p.host_len, "htt");
}

static void test_a_null_url_is_rejected(void) {
  TEST_ASSERT_FALSE(eg_parse_url(NULL, &p));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_scheme_host_and_path);
  RUN_TEST(test_scheme_match_is_case_insensitive);
  RUN_TEST(test_a_url_with_no_scheme_parses);
  RUN_TEST(test_https_is_rejected);
  RUN_TEST(test_a_port_before_a_path_is_taken);
  RUN_TEST(test_a_port_with_no_path_is_taken);
  RUN_TEST(test_a_port_before_a_query_is_taken);
  RUN_TEST(test_a_colon_inside_the_query_is_not_a_port);
  RUN_TEST(test_a_query_with_no_path_yields_a_root_path);
  RUN_TEST(test_a_slash_inside_the_query_is_not_a_path);
  RUN_TEST(test_path_and_query_split_at_the_question_mark);
  RUN_TEST(test_bare_host_yields_a_root_path);
  RUN_TEST(test_an_empty_url_parses_to_empty_parts);
  RUN_TEST(test_a_scheme_with_nothing_after_it_parses);
  RUN_TEST(test_a_truncated_scheme_is_treated_as_a_host);
  RUN_TEST(test_a_null_url_is_rejected);
  return UNITY_END();
}
