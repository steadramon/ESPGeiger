#include "StringUtil.h"

#include <string.h>

int format_f(char* buf, size_t bufsz, float v, uint8_t decimals) {
  uint32_t bits;
  memcpy(&bits, &v, sizeof(bits));
  // Sign written by hand: a "-" literal would sit in .rodata, which is DRAM.
  size_t o = 0;
  if ((bits >> 31) && bufsz > 1) buf[o++] = '-';
  int neg = (int)(bits >> 31);
  bits &= 0x7FFFFFFFu;
  if (bits >= 0x7F800000u)
    return neg + snprintf_P(buf + o, bufsz - o, bits > 0x7F800000u ? PSTR("nan") : PSTR("inf"));
  if (bits >= 0x4F800000u) bits = 0x4F7FFFFFu;   // saturate below 2^32
  memcpy(&v, &bits, sizeof(v));

  // The integer part and the fraction are both exact; the fraction is then
  // rounded half to even on its exact binary value, as printf does.
  uint32_t ip = (uint32_t)v;
  float f = v - (float)ip;
  uint32_t scale = 1;
  for (uint8_t i = 0; i < decimals; i++) scale *= 10;
  uint32_t fb;
  memcpy(&fb, &f, sizeof(fb));
  uint32_t q = 0;
  if (fb) {
    uint64_t m = (fb & 0x7FFFFFu) | 0x800000u;
    int sh = 150 - (int)(fb >> 23);   // f = m / 2^sh; subnormals give sh >= 64
    if (sh < 64) {
      uint64_t p = m * scale;
      uint64_t qq = p >> sh;
      uint64_t r = p - (qq << sh);
      uint64_t half = (uint64_t)1 << (sh - 1);
      if (r > half || (r == half && (qq & 1))) qq++;
      q = (uint32_t)qq;
    }
  }
  if (q >= scale) { q -= scale; ip++; }
  return neg + snprintf_P(buf + o, bufsz - o, PSTR("%lu.%0*lu"),
                          (unsigned long)ip, (int)decimals, (unsigned long)q);
}
