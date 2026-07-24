#include "StringUtil.h"

int format_f(char* buf, size_t bufsz, float v, uint8_t decimals) {
  if (v < 0) v = 0;
  int32_t scale = 1;
  for (uint8_t i = 0; i < decimals; i++) scale *= 10;
  int32_t s = (int32_t)(v * (float)scale + 0.5f);
  return snprintf_P(buf, bufsz, PSTR("%ld.%0*ld"),
                    (long)(s / scale), (int)decimals, (long)(s % scale));
}
