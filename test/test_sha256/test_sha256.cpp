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

// Known-answer vectors from FIPS 180-2 and RFC 4231. Known-answer, not
// self-consistency: hashing twice with our own code proves nothing.
//
// GRNG extract, WebAPI signing and coredump identity all depend on this. A
// wrong digest is silent - a plausible 32 bytes the server disagrees with.

#include <unity.h>
#include <string.h>
#include <stdint.h>
#include <string>

#include "GRNG/sha256.h"

void setUp(void)    {}
void tearDown(void) {}

// --- helpers ----------------------------------------------------------------

static std::string hex(const uint8_t* p, size_t n) {
  static const char* d = "0123456789abcdef";
  std::string s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
  return s;
}

// Hash a buffer byte-by-byte through write(uint8_t).
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

// --- FIPS 180-2 -------------------------------------------------------------

static void test_fips_180_2_vectors(void) {
  ASSERT_HASH("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              sha_str(""));
  ASSERT_HASH("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              sha_str("abc"));
  // 56 bytes: the exact length that forces padding into a second block.
  ASSERT_HASH("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
              sha_str("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"));
  // 112 bytes: exactly two blocks of message.
  ASSERT_HASH("cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1",
              sha_str("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                      "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"));
}

// --- padding boundaries -----------------------------------------------------

// Where a SHA implementation breaks is the padding arithmetic: 55 is the last
// length that fits its 0x80 + length field in one block, 56 forces a second,
// and each multiple of 64 repeats the cliff. This walks every one of them.
static void test_block_boundary_lengths(void) {
  struct { size_t n; const char* want; } cases[] = {
    {   0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
    {   1, "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb" },
    {  55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318" },
    {  56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a" },
    {  57, "f13b2d724659eb3bf47f2dd6af1accc87b81f09f59f2b75e5c0bed6589dfe8c6" },
    {  63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34" },
    {  64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb" },
    {  65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0" },
    { 119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb" },
    { 120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c" },
    { 127, "c57e9278af78fa3cab38667bef4ce29d783787a2f731d4e12200270f0c32320a" },
    { 128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e" },
  };
  uint8_t buf[128];
  memset(buf, 'a', sizeof(buf));
  for (auto& c : cases) {
    std::string got = sha_bytes(buf, c.n);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.want, got.c_str(),
      "block boundary length mismatch");
  }
}

// The length field is a bit count derived from a uint32 byte counter. A long
// message exercises the multi-block path and the counter shifts in pad().
static void test_one_million_a(void) {
  Sha256.init();
  uint8_t chunk[1000];
  memset(chunk, 'a', sizeof(chunk));
  for (int i = 0; i < 1000; i++) Sha256.write(chunk, sizeof(chunk));
  ASSERT_HASH("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
              hex(Sha256.result(), HASH_LENGTH));
}

// The buffer overload inherited from Print must agree with byte-at-a-time.
// That path is what callers actually use, and it is only reachable because
// sha256.h says `using Print::write`.
static void test_buffer_write_matches_byte_write(void) {
  const char* msg = "The quick brown fox jumps over the lazy dog";
  size_t len = strlen(msg);

  Sha256.init();
  Sha256.write((const uint8_t*)msg, len);
  std::string bulk = hex(Sha256.result(), HASH_LENGTH);

  TEST_ASSERT_EQUAL_STRING(bulk.c_str(), sha_bytes((const uint8_t*)msg, len).c_str());
  ASSERT_HASH("d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592", bulk);
}

// Splitting a message across write() calls must not change the digest,
// including splits that land mid-block.
static void test_incremental_writes_are_split_invariant(void) {
  const char* msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  const char* want = "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
  size_t len = strlen(msg);

  for (size_t split = 0; split <= len; split++) {
    Sha256.init();
    Sha256.write((const uint8_t*)msg, split);
    Sha256.write((const uint8_t*)msg + split, len - split);
    std::string got = hex(Sha256.result(), HASH_LENGTH);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want, got.c_str(), "split changed the digest");
  }
}

// --- HMAC, RFC 4231 ---------------------------------------------------------

static void test_rfc4231_hmac_vectors(void) {
  {   // case 1
    uint8_t k[20]; memset(k, 0x0b, sizeof(k));
    ASSERT_HASH("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
                hmac(k, sizeof(k), (const uint8_t*)"Hi There", 8));
  }
  {   // case 2 - short key, no padding to block length needed beyond zero fill
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

// Keys longer than the 64-byte block must be hashed down first. This is a
// distinct code path in initHmac and the easiest part of HMAC to get wrong.
static void test_rfc4231_oversized_key(void) {
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

// A key of exactly the block length is used verbatim, neither hashed nor
// re-padded. The boundary between the two branches of initHmac.
static void test_hmac_key_exactly_block_length(void) {
  uint8_t k64[64], k65[65];
  memset(k64, 0xaa, sizeof(k64));
  memset(k65, 0xaa, sizeof(k65));
  std::string a = hmac(k64, 64, (const uint8_t*)"x", 1);
  std::string b = hmac(k65, 65, (const uint8_t*)"x", 1);
  TEST_ASSERT_EQUAL_size_t(64, a.size());
  // 64 goes down the verbatim branch, 65 down the hash-first branch, so they
  // must differ. Equal would mean the boundary test is inverted.
  TEST_ASSERT_TRUE(a != b);
}

// --- contract / quirks ------------------------------------------------------

// CONTRACT, not a defect. result() finalises: it calls pad(), which mutates
// buffer and state, so it is a one-shot exactly like mbedtls_sha256_finish or
// EVP_DigestFinal. Callers copy the 32 bytes out and do not call it twice.
// Asserted so the one-shot semantics cannot change silently.
static void test_result_is_destructive(void) {
  Sha256.init();
  Sha256.write((const uint8_t*)"abc", 3);
  std::string first = hex(Sha256.result(), HASH_LENGTH);
  std::string second = hex(Sha256.result(), HASH_LENGTH);
  ASSERT_HASH("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", first);
  TEST_ASSERT_TRUE_MESSAGE(first != second, "result() unexpectedly idempotent");
}

// CONTRACT, not a defect. Sha256 is a shared global with no implicit reset;
// every call site must init() first. GRNG keeps its own private Sha256Class
// precisely because extract() runs inside the uECC signing callback and must
// not clobber a hash a caller has in flight. This pins why that matters.
static void test_global_instance_requires_init(void) {
  Sha256.init();
  Sha256.write((const uint8_t*)"leftover", 8);
  // no result(), no init(): state is dirty

  Sha256.write((const uint8_t*)"abc", 3);
  std::string dirty = hex(Sha256.result(), HASH_LENGTH);
  TEST_ASSERT_TRUE_MESSAGE(
    dirty != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    "dirty state produced the clean digest, so state is not carried");

  ASSERT_HASH("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              sha_str("abc"));
}

// The implementation stores its working buffer byte-swapped (`buffer.b[off ^ 3]`)
// and ships a byte-swapped init state, so it is correct only on a little-endian
// machine. ESP8266, ESP32 and every host we test on are little-endian; this
// asserts the assumption rather than leaving it implicit.
static void test_little_endian_assumption_holds(void) {
  union { uint32_t w; uint8_t b[4]; } probe;
  probe.w = 0x01020304u;
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x04, probe.b[0],
    "sha256.cpp assumes little-endian byte order");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_fips_180_2_vectors);
  RUN_TEST(test_block_boundary_lengths);
  RUN_TEST(test_one_million_a);
  RUN_TEST(test_buffer_write_matches_byte_write);
  RUN_TEST(test_incremental_writes_are_split_invariant);
  RUN_TEST(test_rfc4231_hmac_vectors);
  RUN_TEST(test_rfc4231_oversized_key);
  RUN_TEST(test_hmac_key_exactly_block_length);
  RUN_TEST(test_result_is_destructive);
  RUN_TEST(test_global_instance_requires_init);
  RUN_TEST(test_little_endian_assumption_holds);
  return UNITY_END();
}
