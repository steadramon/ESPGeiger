#include "GRNG.h"
#include "../Logger/Logger.h"

GRNG::GRNG() {
};

void GRNG::begin() {
#ifdef ESP8266
  randomSeed(RANDOM_REG32);
#else
  randomSeed(esp_random());
#endif
}
