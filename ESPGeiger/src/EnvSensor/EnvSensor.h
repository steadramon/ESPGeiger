/*
  EnvSensor.h - BME280 / BMP280 / AHT-family environment module.

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
#ifndef ENVSENSOR_H
#define ENVSENSOR_H

#include <Arduino.h>
#include <BoschTHP.h>
#include <AsairAHT.h>
#include "../Module/EGModule.h"
#include "../Util/Globals.h"

// If OLED is in the build it owns the I2C pin layout - share its pins so
// EnvSensor doesn't reconfigure Wire underneath the display. Platform
// defaults only kick in on headless builds (or where the user explicitly
// overrides ENV_DEFAULT_*).
#ifndef ENV_DEFAULT_SDA
  #if defined(OLED_SDA)
    #define ENV_DEFAULT_SDA OLED_SDA
  #elif defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S2)
    #define ENV_DEFAULT_SDA 8
  #elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
    #define ENV_DEFAULT_SDA 6
  #elif defined(ESP32)
    #define ENV_DEFAULT_SDA 21
  #else
    #define ENV_DEFAULT_SDA 4
  #endif
#endif

#ifndef ENV_DEFAULT_SCL
  #if defined(OLED_SCL)
    #define ENV_DEFAULT_SCL OLED_SCL
  #elif defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S2)
    #define ENV_DEFAULT_SCL 9
  #elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
    #define ENV_DEFAULT_SCL 7
  #elif defined(ESP32)
    #define ENV_DEFAULT_SCL 22
  #else
    #define ENV_DEFAULT_SCL 5
  #endif
#endif

#define ENV_SAMPLE_MS    10000UL
#define ENV_EMA_ALPHA     0.1f

class EnvSensor : public EGModule {
  public:
    enum Unit : uint8_t { UNIT_C = 0, UNIT_F = 1, UNIT_K = 2 };

    EnvSensor() {}
    const char* name() override { return "env"; }
    uint8_t priority() override { return EG_PRIORITY_DEFAULT; }
    // /param render order - just below the Input group (12).
    uint8_t display_order() override { return 13; }
    uint16_t warmup_seconds() override { return 0; }
    bool has_loop() override { return true; }
    uint16_t loop_interval_ms() override { return ENV_SAMPLE_MS; }
    void begin() override;
    void loop(unsigned long now) override;
    const EGPrefGroup* prefs_group() override;
    void on_prefs_loaded() override;
    void on_prefs_saved() override;
    size_t status_json(char* buf, size_t cap, unsigned long now) override;

    static constexpr uint8_t DRV_BOSCH = 1 << 0;
    static constexpr uint8_t DRV_AHT   = 1 << 1;

    bool    present() const { return _drv_flags != 0 && _st != nullptr; }
    uint8_t unit() const    { return _unit; }
    const __FlashStringHelper* chipName() const;
    uint8_t driverFlags() const { return _drv_flags; }

    float tempC() const;
    float humidity() const;
    float pressure() const;
    float tempUser() const;

  private:
    struct State {
      float ema_t = NAN;
      float ema_h = NAN;
      float ema_p = NAN;
    };

    void readPrefs();
    void sample();
    void emaUpdate(float t, float h, float p);
    bool tryDetect();

    uint8_t  _sda = ENV_DEFAULT_SDA;
    uint8_t  _scl = ENV_DEFAULT_SCL;
    uint8_t  _unit = UNIT_C;
    bool     _started = false;
    uint8_t  _drv_flags = 0;
    uint8_t  _detect_tries = 0;
    BoschTHP::Sensor _bosch;
    AsairAHT::Sensor _aht;
    State*   _st = nullptr;
};

extern EnvSensor envsensor;

#endif
