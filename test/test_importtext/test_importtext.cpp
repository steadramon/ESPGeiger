/*
  test_importtext.cpp - module.key=value lines.

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
#include <stdlib.h>
#include <string.h>
#include <string>
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/WebPortal/ImportText.h"

static const char* const MOVED[] = { "old1", "old2" };
const char* EGPrefs::moved_from(size_t i) { return i < 2 ? MOVED[i] : nullptr; }

// every put, as "mid|key|val"
static std::string s_puts[8];
static int s_count = 0;

static bool put(const char* mid, const char* key, const char* val) {
  if (strcmp(mid, "no") == 0) return false;
  if (s_count < 8) s_puts[s_count] = std::string(mid) + "|" + key + "|" + val;
  s_count++;
  return true;
}

static ImportText::Result run(const char* text) {
  return ImportText::apply(text, strlen(text), put);
}

void setUp(void) {
  s_count = 0;
  for (auto& p : s_puts) p.clear();
}
void tearDown(void) {}

static void lines(void) {
  ImportText::Result r = run("geiger.rxpin=4\nmax7219.din=6\n");
  TEST_ASSERT_EQUAL_INT(2, r.applied);
  TEST_ASSERT_EQUAL_INT(0, r.skipped);
  TEST_ASSERT_EQUAL_STRING("geiger|rxpin|4", s_puts[0].c_str());
  TEST_ASSERT_EQUAL_STRING("max7219|din|6", s_puts[1].c_str());
}

static void crlf(void) {
  ImportText::Result r = run("a.b=1\r\nc.d=2");
  TEST_ASSERT_EQUAL_INT(2, r.applied);
  TEST_ASSERT_EQUAL_STRING("a|b|1", s_puts[0].c_str());
  TEST_ASSERT_EQUAL_STRING("c|d|2", s_puts[1].c_str());
}

static void comments(void) {
  ImportText::Result r = run("# setup\n\n  # indented\n\r\n   \n  a.b=1\n");
  TEST_ASSERT_EQUAL_INT(1, r.applied);
  TEST_ASSERT_EQUAL_INT(0, r.skipped);
  TEST_ASSERT_EQUAL_STRING("a|b|1", s_puts[0].c_str());
}

static void value_eq(void) {
  run("hook.url=http://x.y/a?b=c.d\n");
  TEST_ASSERT_EQUAL_STRING("hook|url|http://x.y/a?b=c.d", s_puts[0].c_str());
}

static void empty_value(void) {
  ImportText::Result r = run("a.b=\n");
  TEST_ASSERT_EQUAL_INT(1, r.applied);
  TEST_ASSERT_EQUAL_STRING("a|b|", s_puts[0].c_str());
}

static void bad_lines(void) {
  ImportText::Result r = run("ab=1\na.b\n.b=1\na.=1\n=1\nESPGxyz\n");
  TEST_ASSERT_EQUAL_INT(0, r.applied);
  TEST_ASSERT_EQUAL_INT(6, r.skipped);
  TEST_ASSERT_EQUAL_INT(0, s_count);
}

static void refused(void) {
  ImportText::Result r = run("no.b=1\na.b=2\n");
  TEST_ASSERT_EQUAL_INT(1, r.applied);
  TEST_ASSERT_EQUAL_INT(1, r.skipped);
}

static void duplicate(void) {
  ImportText::Result r = run("a.b=1\na.b=2\n");
  TEST_ASSERT_EQUAL_INT(2, r.applied);
  TEST_ASSERT_EQUAL_STRING("a|b|2", s_puts[1].c_str());
}

static void too_long(void) {
  std::string id(EGPREF_ID_MAX, 'k');
  std::string val(EGPREFS_MAX_VALUE, 'v');
  std::string ok = id + "." + id + "=" + val + "\n";
  std::string text = ok + id + "k.b=1\na." + id + "k=1\na.b=" + val + "v\n";
  ImportText::Result r = ImportText::apply(text.c_str(), text.size(), put);
  TEST_ASSERT_EQUAL_INT(1, r.applied);
  TEST_ASSERT_EQUAL_INT(3, r.skipped);
  TEST_ASSERT_EQUAL_INT(2, r.first_skip);
  TEST_ASSERT_EQUAL_STRING((id + "|" + id + "|" + val).c_str(), s_puts[0].c_str());
}

// comments and blanks still count as lines
static void skip_line(void) {
  const char* text = "# c\n\na.b=1\nbad\nc.d=2\nworse";
  ImportText::Result r = ImportText::apply(text, strlen(text), put);
  TEST_ASSERT_EQUAL_INT(2, r.applied);
  TEST_ASSERT_EQUAL_INT(2, r.skipped);
  TEST_ASSERT_EQUAL_INT(4, r.first_skip);
  r = ImportText::apply("a.b=1", 5, put);
  TEST_ASSERT_EQUAL_INT(0, r.first_skip);
}

// a NUL skips the line, never cuts the value
static void nul_byte(void) {
  const char text[] = "a.b=1\0x\nc.d=2";
  ImportText::Result r = ImportText::apply(text, sizeof(text) - 1, put);
  TEST_ASSERT_EQUAL_INT(1, r.applied);
  TEST_ASSERT_EQUAL_INT(1, r.skipped);
  TEST_ASSERT_EQUAL_STRING("c|d|2", s_puts[0].c_str());
}

// only len is read
static void no_overread(void) {
  ImportText::Result r = ImportText::apply("a.b=12c.d=3", 5, put);
  TEST_ASSERT_EQUAL_INT(1, r.applied);
  TEST_ASSERT_EQUAL_STRING("a|b|1", s_puts[0].c_str());
  // no NUL after, asan catches a read past
  char* raw = (char*)malloc(3);
  memcpy(raw, "a.b", 3);
  r = ImportText::apply(raw, 3, put);
  free(raw);
  TEST_ASSERT_EQUAL_INT(1, r.skipped);
}

static void blob(void) {
  TEST_ASSERT_TRUE(ImportText::is_blob("ESPGAQ==", 8));
  TEST_ASSERT_FALSE(ImportText::is_blob("ESP", 3));
  TEST_ASSERT_FALSE(ImportText::is_blob("ESPx", 4));
  TEST_ASSERT_FALSE(ImportText::is_blob("ESPG", 3));
  TEST_ASSERT_FALSE(ImportText::is_blob("espg.x=1", 8));
  TEST_ASSERT_FALSE(ImportText::is_blob(" ESPG", 5));
}

// blob records: len key, u16 BE len value
static std::string brec(const char* key, const std::string& val) {
  std::string r(1, (char)strlen(key));
  r += key;
  r += (char)(val.size() >> 8);
  r += (char)(val.size() & 0xFF);
  return r + val;
}

static std::string bgroup(const char* mid, int n, const std::string& recs) {
  std::string g(1, (char)strlen(mid));
  g += mid;
  g += (char)n;
  return g + recs;
}

static int run_blob(const std::string& b) {
  return ImportText::apply_blob((const uint8_t*)b.data(), b.size(), put);
}

static void blob_walk(void) {
  std::string b = std::string(1, 2) + bgroup("a", 2, brec("b", "1") + brec("c", "")) +
                  bgroup("no", 1, brec("x", "9"));
  TEST_ASSERT_NULL(ImportText::blob_error((const uint8_t*)b.data(), b.size()));
  TEST_ASSERT_EQUAL_INT(2, run_blob(b));
  TEST_ASSERT_EQUAL_STRING("a|b|1", s_puts[0].c_str());
  TEST_ASSERT_EQUAL_STRING("a|c|", s_puts[1].c_str());
}

// long id or value skipped, rest land
static void blob_long(void) {
  std::string id(EGPREF_ID_MAX + 1, 'k');
  std::string val(EGPREFS_MAX_VALUE + 1, 'v');
  std::string b = std::string(1, 1) +
                  bgroup("a", 3, brec(id.c_str(), "1") + brec("b", val) + brec("c", "2"));
  TEST_ASSERT_EQUAL_INT(2, run_blob(b));
  TEST_ASSERT_EQUAL_STRING("a||1", s_puts[0].c_str());
  TEST_ASSERT_EQUAL_STRING("a|c|2", s_puts[1].c_str());
}

// every cut is refused before any put
static void blob_cut(void) {
  std::string b = std::string(1, 1) + bgroup("a", 1, brec("b", "12"));
  for (size_t n = 0; n < b.size(); n++) {
    uint8_t* raw = (uint8_t*)malloc(n ? n : 1);
    memcpy(raw, b.data(), n);
    TEST_ASSERT_NOT_NULL(ImportText::blob_error(raw, n));
    TEST_ASSERT_EQUAL_INT(-1, ImportText::apply_blob(raw, n, put));
    free(raw);
  }
  TEST_ASSERT_EQUAL_INT(0, s_count);
  TEST_ASSERT_EQUAL_INT(1, run_blob(b));
}

// later puts win
static void blob_order(void) {
  std::string b = std::string(1, 3) + bgroup("a", 1, brec("b", "1")) +
                  bgroup("old1", 1, brec("b", "2")) + bgroup("old2", 1, brec("b", "3"));
  TEST_ASSERT_EQUAL_INT(3, run_blob(b));
  TEST_ASSERT_EQUAL_STRING("old2|b|3", s_puts[0].c_str());
  TEST_ASSERT_EQUAL_STRING("old1|b|2", s_puts[1].c_str());
  TEST_ASSERT_EQUAL_STRING("a|b|1", s_puts[2].c_str());
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(lines);
  RUN_TEST(crlf);
  RUN_TEST(comments);
  RUN_TEST(value_eq);
  RUN_TEST(empty_value);
  RUN_TEST(bad_lines);
  RUN_TEST(refused);
  RUN_TEST(duplicate);
  RUN_TEST(too_long);
  RUN_TEST(skip_line);
  RUN_TEST(nul_byte);
  RUN_TEST(no_overread);
  RUN_TEST(blob);
  RUN_TEST(blob_walk);
  RUN_TEST(blob_long);
  RUN_TEST(blob_cut);
  RUN_TEST(blob_order);
  return UNITY_END();
}
