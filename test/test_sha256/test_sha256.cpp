/*
  test_sha256 - the vendored SHA-256 / HMAC-SHA-256 in src/GRNG.

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

// FIPS 180-2 and RFC 4231 vectors

#include <unity.h>
#include <string.h>
#include <stdint.h>
#include <string>

#include "GRNG/sha256.h"

static Sha256Class Sha256;

void setUp(void)    {}
void tearDown(void) {}

static std::string hex(const uint8_t* p, size_t n) {
  static const char* d = "0123456789abcdef";
  std::string s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
  return s;
}

static std::string sha_bytes(const uint8_t* p, size_t n) {
  Sha256.init();
  for (size_t i = 0; i < n; i++) Sha256.write(p[i]);
  return hex(Sha256.result(), HASH_LENGTH);
}

static std::string sha_str(const char* s) {
  return sha_bytes((const uint8_t*)s, strlen(s));
}

static std::string hmac(const uint8_t* key, int klen, const uint8_t* msg, size_t mlen) {
  Sha256.initHmac(key, klen);
  for (size_t i = 0; i < mlen; i++) Sha256.write(msg[i]);
  return hex(Sha256.resultHmac(), HASH_LENGTH);
}

#define ASSERT_HASH(expect, actual) \
  TEST_ASSERT_EQUAL_STRING((expect), (actual).c_str())

static void fips_vectors(void) {
  ASSERT_HASH("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              sha_str(""));
  ASSERT_HASH("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              sha_str("abc"));
  // 56: pads into a second block
  ASSERT_HASH("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
              sha_str("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"));
  // 112 bytes: two blocks
  ASSERT_HASH("cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1",
              sha_str("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                      "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"));
}

static void million_a(void) {
  Sha256.init();
  uint8_t chunk[1000];
  memset(chunk, 'a', sizeof(chunk));
  for (int i = 0; i < 1000; i++) Sha256.write(chunk, sizeof(chunk));
  ASSERT_HASH("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
              hex(Sha256.result(), HASH_LENGTH));
}

static void buffer_write(void) {
  const char* msg = "The quick brown fox jumps over the lazy dog";
  size_t len = strlen(msg);

  Sha256.init();
  Sha256.write((const uint8_t*)msg, len);
  std::string bulk = hex(Sha256.result(), HASH_LENGTH);

  TEST_ASSERT_EQUAL_STRING(bulk.c_str(), sha_bytes((const uint8_t*)msg, len).c_str());
  ASSERT_HASH("d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592", bulk);
}

static void rfc4231(void) {
  {   // case 1
    uint8_t k[20]; memset(k, 0x0b, sizeof(k));
    ASSERT_HASH("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
                hmac(k, sizeof(k), (const uint8_t*)"Hi There", 8));
  }
  {   // case 2
    ASSERT_HASH("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
                hmac((const uint8_t*)"Jefe", 4,
                     (const uint8_t*)"what do ya want for nothing?", 28));
  }
  {   // case 3
    uint8_t k[20], d[50];
    memset(k, 0xaa, sizeof(k)); memset(d, 0xdd, sizeof(d));
    ASSERT_HASH("773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe",
                hmac(k, sizeof(k), d, sizeof(d)));
  }
  {   // case 4 - key is 1..25
    uint8_t k[25], d[50];
    for (int i = 0; i < 25; i++) k[i] = (uint8_t)(i + 1);
    memset(d, 0xcd, sizeof(d));
    ASSERT_HASH("82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b",
                hmac(k, sizeof(k), d, sizeof(d)));
  }
}

static void long_key(void) {
  uint8_t k[131];
  memset(k, 0xaa, sizeof(k));

  const char* d5 = "Test Using Larger Than Block-Size Key - Hash Key First";
  ASSERT_HASH("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
              hmac(k, sizeof(k), (const uint8_t*)d5, strlen(d5)));

  const char* d6 =
    "This is a test using a larger than block-size key and a larger than "
    "block-size data. The key needs to be hashed before being used by the "
    "HMAC algorithm.";
  ASSERT_HASH("9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2",
              hmac(k, sizeof(k), (const uint8_t*)d6, strlen(d6)));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(fips_vectors);
  RUN_TEST(million_a);
  RUN_TEST(buffer_write);
  RUN_TEST(rfc4231);
  RUN_TEST(long_key);
  return UNITY_END();
}
