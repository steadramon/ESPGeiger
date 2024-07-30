#include "GRand.h"
#include "../Logger/Logger.h"

GRand::GRand() {
};

void GRand::begin() {
  Log::console(PSTR("GRand: Initial seed ..."));
#ifdef ESP8266
  randomSeed(RANDOM_REG32);
#else
  randomSeed(esp_random());
#endif
}
