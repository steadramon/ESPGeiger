/*
  MsgPack.h - Header-only MessagePack encoder + decoder.

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
#ifndef ESPGEIGER_MSGPACK_H
#define ESPGEIGER_MSGPACK_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace MsgPack {

// Type tags (the bytes that identify a value's encoding).
static constexpr uint8_t TAG_NIL     = 0xc0;
static constexpr uint8_t TAG_FALSE   = 0xc2;
static constexpr uint8_t TAG_TRUE    = 0xc3;
static constexpr uint8_t TAG_BIN8    = 0xc4;
static constexpr uint8_t TAG_BIN16   = 0xc5;
static constexpr uint8_t TAG_FLOAT32 = 0xca;
static constexpr uint8_t TAG_UINT8   = 0xcc;
static constexpr uint8_t TAG_UINT16  = 0xcd;
static constexpr uint8_t TAG_UINT32  = 0xce;
static constexpr uint8_t TAG_INT8    = 0xd0;
static constexpr uint8_t TAG_INT16   = 0xd1;
static constexpr uint8_t TAG_INT32   = 0xd2;
static constexpr uint8_t TAG_STR8    = 0xd9;
static constexpr uint8_t TAG_STR16   = 0xda;
static constexpr uint8_t TAG_ARRAY16 = 0xdc;
static constexpr uint8_t TAG_MAP16   = 0xde;
static constexpr uint8_t TAG_BIN32   = 0xc6;
static constexpr uint8_t TAG_EXT8    = 0xc7;
static constexpr uint8_t TAG_EXT16   = 0xc8;
static constexpr uint8_t TAG_EXT32   = 0xc9;
static constexpr uint8_t TAG_FLOAT64 = 0xcb;
static constexpr uint8_t TAG_UINT64  = 0xcf;
static constexpr uint8_t TAG_INT64   = 0xd3;
static constexpr uint8_t TAG_FIXEXT1 = 0xd4;
static constexpr uint8_t TAG_FIXEXT16 = 0xd8;
static constexpr uint8_t TAG_STR32   = 0xdb;
static constexpr uint8_t TAG_ARRAY32 = 0xdd;
static constexpr uint8_t TAG_MAP32   = 0xdf;

// Float bit-pattern punning - no libm, no copy via memcpy in tight code.
static inline uint32_t f32_bits(float f) {
  union { float f; uint32_t u; } x;
  x.f = f; return x.u;
}
static inline float bits_to_f32(uint32_t u) {
  union { float f; uint32_t u; } x;
  x.u = u; return x.f;
}

// ---------------------------------------------------------------------------
// Writer - encode MessagePack into a caller-supplied buffer.
//
// Usage:
//   uint8_t buf[256];
//   MsgPack::Writer mp(buf, sizeof(buf));
//   mp.map(3);
//   mp.kv("cid", chip_id);
//   mp.kv("cpm", 42);
//   mp.kv("usv", 0.21f);
//   if (mp.overflow) { ... }
//   send(buf, mp.length());
// ---------------------------------------------------------------------------
class Writer {
public:
  uint8_t* buf;
  size_t   cap;
  size_t   pos;
  bool     overflow;

  Writer(uint8_t* b, size_t c) : buf(b), cap(c), pos(0), overflow(false) {}

  size_t length() const { return pos; }

  // ---- containers -------------------------------------------------------
  void map(uint16_t n) {
    if (n <= 15) put_u8((uint8_t)(0x80 | n));
    else { put_u8(TAG_MAP16); put_u16(n); }
  }
  void array(uint16_t n) {
    if (n <= 15) put_u8((uint8_t)(0x90 | n));
    else { put_u8(TAG_ARRAY16); put_u16(n); }
  }

  // ---- scalars (explicit width) -----------------------------------------
  void nil()             { put_u8(TAG_NIL); }
  void b(bool v)         { put_u8(v ? TAG_TRUE : TAG_FALSE); }
  void f32(float v)      { put_u8(TAG_FLOAT32); put_u32(f32_bits(v)); }

  // ---- ints (auto-narrow to smallest viable encoding) -------------------
  void uint(uint32_t v) {
    if      (v <= 0x7f)    put_u8((uint8_t)v);             // pos fixint
    else if (v <= 0xff)    { put_u8(TAG_UINT8);  put_u8((uint8_t)v); }
    else if (v <= 0xffff)  { put_u8(TAG_UINT16); put_u16((uint16_t)v); }
    else                   { put_u8(TAG_UINT32); put_u32(v); }
  }
  void sint(int32_t v) {
    if (v >= 0) { uint((uint32_t)v); return; }
    if      (v >= -32)     put_u8((uint8_t)(0xe0 | (v & 0x1f)));   // neg fixint
    else if (v >= -128)    { put_u8(TAG_INT8);  put_u8((uint8_t)(int8_t)v); }
    else if (v >= -32768)  { put_u8(TAG_INT16); put_u16((uint16_t)(int16_t)v); }
    else                   { put_u8(TAG_INT32); put_u32((uint32_t)v); }
  }

  // ---- strings + binary -------------------------------------------------
  void str(const char* s)              { str(s, s ? strlen(s) : 0); }
  void str(const char* s, size_t len) {
    if (len <= 31)         put_u8((uint8_t)(0xa0 | len));   // fixstr
    else if (len <= 0xff)  { put_u8(TAG_STR8);  put_u8((uint8_t)len); }
    else                   { put_u8(TAG_STR16); put_u16((uint16_t)len); }
    put_bytes((const uint8_t*)s, len);
  }
  void bin(const uint8_t* p, size_t len) {
    if (len <= 0xff)       { put_u8(TAG_BIN8);  put_u8((uint8_t)len); }
    else                   { put_u8(TAG_BIN16); put_u16((uint16_t)len); }
    put_bytes(p, len);
  }

  // ---- key + key/value shorthand ---------------------------------------
  void key(const char* k) { str(k); }

  void kv(const char* k, int32_t v)               { key(k); sint(v); }
  void kv(const char* k, uint32_t v)              { key(k); uint(v); }
  void kv(const char* k, float v)                 { key(k); f32(v); }
  void kv(const char* k, double v)                { key(k); f32((float)v); }
  void kv(const char* k, bool v)                  { key(k); b(v); }
  void kv(const char* k, const char* v)           { key(k); v ? str(v) : nil(); }
  void kv(const char* k, const uint8_t* p, size_t len) { key(k); bin(p, len); }

private:
  void put_u8(uint8_t v) {
    if (pos + 1 > cap) { overflow = true; return; }
    buf[pos++] = v;
  }
  void put_u16(uint16_t v) {
    if (pos + 2 > cap) { overflow = true; return; }
    buf[pos++] = (uint8_t)(v >> 8);
    buf[pos++] = (uint8_t)v;
  }
  void put_u32(uint32_t v) {
    if (pos + 4 > cap) { overflow = true; return; }
    buf[pos++] = (uint8_t)(v >> 24);
    buf[pos++] = (uint8_t)(v >> 16);
    buf[pos++] = (uint8_t)(v >> 8);
    buf[pos++] = (uint8_t)v;
  }
  void put_bytes(const uint8_t* p, size_t len) {
    if (pos + len > cap) { overflow = true; return; }
    if (p && len) memcpy(buf + pos, p, len);
    pos += len;
  }
};

// ---------------------------------------------------------------------------
// Reader - decode MessagePack from a caller-supplied buffer.
//
// Usage:
//   MsgPack::Reader r(buf, len);
//   if (!r.find_key("sid")) return;
//   int32_t sid; if (!r.read_int(&sid)) return;
// ---------------------------------------------------------------------------
class Reader {
public:
  const uint8_t* buf;
  size_t cap;
  size_t pos;
  bool   error;

  Reader(const uint8_t* b, size_t c) : buf(b), cap(c), pos(0), error(false) {}

  bool eof() const { return pos >= cap; }

  // Read a map header. Returns entry count, or 0 with error=true on mismatch.
  uint16_t read_map() {
    uint8_t t;
    if (!peek(&t)) return 0;
    if ((t & 0xf0) == 0x80)  { pos++; return t & 0x0f; }
    if (t == TAG_MAP16)      { pos++; uint16_t n; return get_u16(&n) ? n : 0; }
    if (t == TAG_MAP32)      { pos++; uint32_t n; return get_u32(&n) ? count16(n) : 0; }
    error = true; return 0;
  }
  uint16_t read_array() {
    uint8_t t;
    if (!peek(&t)) return 0;
    if ((t & 0xf0) == 0x90)  { pos++; return t & 0x0f; }
    if (t == TAG_ARRAY16)    { pos++; uint16_t n; return get_u16(&n) ? n : 0; }
    if (t == TAG_ARRAY32)    { pos++; uint32_t n; return get_u32(&n) ? count16(n) : 0; }
    error = true; return 0;
  }

  // Read any int variant into int32_t. Out-of-range = error.
  bool read_int(int32_t* out) {
    uint8_t t;
    if (!get_u8(&t)) return false;
    if ((t & 0x80) == 0)   { *out = (int32_t)t; return true; }    // pos fixint
    if ((t & 0xe0) == 0xe0){ *out = (int32_t)(int8_t)t; return true; }  // neg fixint
    switch (t) {
      case TAG_UINT8:  { uint8_t  v; if (!get_u8(&v))  return false; *out = (int32_t)v; return true; }
      case TAG_UINT16: { uint16_t v; if (!get_u16(&v)) return false; *out = (int32_t)v; return true; }
      case TAG_UINT32: { uint32_t v; if (!get_u32(&v)) return false;
                         if (v > 0x7fffffffu) break; *out = (int32_t)v; return true; }
      case TAG_INT8:   { uint8_t  v; if (!get_u8(&v))  return false; *out = (int32_t)(int8_t)v; return true; }
      case TAG_INT16:  { uint16_t v; if (!get_u16(&v)) return false; *out = (int32_t)(int16_t)v; return true; }
      case TAG_INT32:  { uint32_t v; if (!get_u32(&v)) return false; *out = (int32_t)v; return true; }
    }
    error = true; return false;
  }

  // Not via read_int: that collapses "negative was encoded" and "uint32 above
  // INT32_MAX was encoded" into one result, letting a peer turn -1 into
  // 4294967295. Unsigned tags pass at full width; signed tags reject if < 0.
  bool read_uint(uint32_t* out) {
    uint8_t t;
    if (!get_u8(&t)) return false;
    if ((t & 0x80) == 0)    { *out = t; return true; }          // pos fixint
    if ((t & 0xe0) == 0xe0) { error = true; return false; }     // neg fixint
    switch (t) {
      case TAG_UINT8:  { uint8_t  v; if (!get_u8(&v))  return false; *out = v; return true; }
      case TAG_UINT16: { uint16_t v; if (!get_u16(&v)) return false; *out = v; return true; }
      case TAG_UINT32: { uint32_t v; if (!get_u32(&v)) return false; *out = v; return true; }
      case TAG_INT8:   { uint8_t  v; if (!get_u8(&v))  return false;
                         int8_t  s = (int8_t)v;  if (s < 0) break; *out = (uint32_t)s; return true; }
      case TAG_INT16:  { uint16_t v; if (!get_u16(&v)) return false;
                         int16_t s = (int16_t)v; if (s < 0) break; *out = (uint32_t)s; return true; }
      case TAG_INT32:  { uint32_t v; if (!get_u32(&v)) return false;
                         if ((int32_t)v < 0) break; *out = v; return true; }
    }
    error = true; return false;
  }

  bool read_float(float* out) {
    uint8_t t; if (!get_u8(&t)) return false;
    if (t != TAG_FLOAT32) { error = true; return false; }
    uint32_t bits; if (!get_u32(&bits)) return false;
    *out = bits_to_f32(bits); return true;
  }

  bool read_bool(bool* out) {
    uint8_t t; if (!get_u8(&t)) return false;
    if (t == TAG_TRUE)  { *out = true;  return true; }
    if (t == TAG_FALSE) { *out = false; return true; }
    error = true; return false;
  }

  // Copy string into caller's buffer (NUL-terminated). Returns false on
  // type mismatch or insufficient capacity.
  bool read_str(char* out, size_t out_cap) {
    size_t len; if (!read_str_len(&len)) return false;
    if (len >= out_cap || len > cap - pos) { error = true; return false; }
    if (len) memcpy(out, buf + pos, len);
    out[len] = '\0';
    pos += len;
    return true;
  }

  // Skip the current value, any type in the MessagePack spec. Containers
  // nest at most SKIP_DEPTH deep.
  static constexpr uint8_t SKIP_DEPTH = 16;
  bool skip(uint8_t depth = 0) {
    if (depth > SKIP_DEPTH) { error = true; return false; }
    uint8_t t; if (!get_u8(&t)) return false;
    if ((t & 0x80) == 0)    return true;                    // pos fixint
    if ((t & 0xe0) == 0xe0) return true;                    // neg fixint
    if ((t & 0xf0) == 0x80) return skip_n(2u * (t & 0x0f), depth);  // fixmap
    if ((t & 0xf0) == 0x90) return skip_n(t & 0x0f, depth);          // fixarray
    if ((t & 0xe0) == 0xa0) return advance(t & 0x1f);                // fixstr
    if (t >= TAG_FIXEXT1 && t <= TAG_FIXEXT16)              // type byte + 1..16
      return advance(1u + (1u << (t - TAG_FIXEXT1)));
    uint32_t n;
    switch (t) {
      case TAG_NIL: case TAG_TRUE: case TAG_FALSE: return true;
      case TAG_UINT8:  case TAG_INT8:    return advance(1);
      case TAG_UINT16: case TAG_INT16:   return advance(2);
      case TAG_UINT32: case TAG_INT32:
      case TAG_FLOAT32:                  return advance(4);
      case TAG_UINT64: case TAG_INT64:
      case TAG_FLOAT64:                  return advance(8);
      case TAG_STR8:  case TAG_BIN8:     return get_len(1, &n) && advance(n);
      case TAG_STR16: case TAG_BIN16:    return get_len(2, &n) && advance(n);
      case TAG_STR32: case TAG_BIN32:    return get_len(4, &n) && advance(n);
      case TAG_EXT8:                     return get_len(1, &n) && advance(1) && advance(n);
      case TAG_EXT16:                    return get_len(2, &n) && advance(1) && advance(n);
      case TAG_EXT32:                    return get_len(4, &n) && advance(1) && advance(n);
      case TAG_ARRAY16:                  return get_len(2, &n) && skip_n(n, depth);
      case TAG_ARRAY32:                  return get_len(4, &n) && skip_n(n, depth);
      case TAG_MAP16:                    return get_len(2, &n) && skip_n(2u * n, depth);
      case TAG_MAP32:                    return get_len(4, &n) && n <= 0x7fffffffu && skip_n(2u * n, depth);
    }
    error = true; return false;   // 0xc1, never used
  }

  // Find a key in the map currently at `pos`. On success leaves pos on
  // the value. On miss leaves pos at end of map.
  bool find_key(const char* key) {
    uint16_t n = read_map();
    if (error) return false;
    size_t klen = strlen(key);
    for (uint16_t i = 0; i < n; i++) {
      size_t this_len;
      if (!read_str_len(&this_len)) return false;
      if (this_len == klen && this_len <= cap - pos &&
          memcmp(buf + pos, key, this_len) == 0) {
        pos += this_len;
        return true;
      }
      // Check before advancing, or `pos` lands past `cap` on truncated input.
      if (this_len > cap - pos) { error = true; return false; }
      pos += this_len;
      if (!skip()) return false;
    }
    return false;
  }

private:
  bool peek(uint8_t* out) {
    if (pos >= cap) { error = true; return false; }
    *out = buf[pos]; return true;
  }
  bool get_u8(uint8_t* out) {
    if (pos + 1 > cap) { error = true; return false; }
    *out = buf[pos++]; return true;
  }
  bool get_u16(uint16_t* out) {
    if (pos + 2 > cap) { error = true; return false; }
    *out = ((uint16_t)buf[pos] << 8) | buf[pos + 1];
    pos += 2; return true;
  }
  bool get_u32(uint32_t* out) {
    if (pos + 4 > cap) { error = true; return false; }
    *out = ((uint32_t)buf[pos]     << 24) |
           ((uint32_t)buf[pos + 1] << 16) |
           ((uint32_t)buf[pos + 2] <<  8) |
            (uint32_t)buf[pos + 3];
    pos += 4; return true;
  }
  bool advance(size_t n) {
    if (n > cap - pos) { error = true; return false; }
    pos += n; return true;
  }
  bool get_len(uint8_t width, uint32_t* out) {
    if (width == 1) { uint8_t  v; if (!get_u8(&v))  return false; *out = v; return true; }
    if (width == 2) { uint16_t v; if (!get_u16(&v)) return false; *out = v; return true; }
    return get_u32(out);
  }
  // Each element is at least one byte, so a count past the buffer fails at
  // the end of the data, not after 2^32 iterations.
  bool skip_n(uint32_t n, uint8_t depth) {
    if (n > cap - pos) { error = true; return false; }
    for (uint32_t i = 0; i < n; i++) if (!skip(depth + 1)) return false;
    return true;
  }
  uint16_t count16(uint32_t n) {
    if (n > 0xffffu) { error = true; return 0; }
    return (uint16_t)n;
  }
  bool read_str_len(size_t* out) {
    uint8_t t; if (!get_u8(&t)) return false;
    if ((t & 0xe0) == 0xa0) { *out = t & 0x1f; return true; }
    if (t == TAG_STR8)      { uint8_t  n; if (!get_u8(&n))  return false; *out = n; return true; }
    if (t == TAG_STR16)     { uint16_t n; if (!get_u16(&n)) return false; *out = n; return true; }
    if (t == TAG_STR32)     { uint32_t n; if (!get_u32(&n)) return false; *out = n; return true; }
    error = true; return false;
  }
};

}

#endif
