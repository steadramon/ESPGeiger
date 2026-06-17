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
#include "../Util/Globals.h"

#ifndef DISABLE_ENVSENSOR
#include <BoschTHP.h>
#include <AsairAHT.h>
#include "../Module/EGModule.h"
#endif

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
// Receiver-mirror staleness window. /env packets arrive every ~10 s from
// UdpBlip; 90 s = 9 missed before the mirror reports absent.
#define ENV_REMOTE_TTL_MS 90000UL

#ifndef DISABLE_ENVSENSOR

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

    static constexpr uint8_t DRV_BOSCH = 1 << 0;
    static constexpr uint8_t DRV_AHT   = 1 << 1;

    bool    present() const { return localPresent() || remoteFresh(); }
    bool    localPresent() const { return _drv_flags != 0 && _st != nullptr; }
    uint8_t unit() const    { return _unit; }
    const __FlashStringHelper* chipName() const;
    uint8_t driverFlags() const { return _drv_flags; }

    float tempC() const;
    float humidity() const;
    float pressure() const;
    float tempUser() const;

    // Push values received over UDP (UdpBlip /env). When no local I2C
    // driver came up, accessors fall through to these so downstream
    // outputs see env data from the upstream producer. Mirror expires
    // ENV_REMOTE_TTL_MS after the last call.
    void setRemote(float t, float h, float p);
    bool remoteFresh() const;

  private:
    struct State {
      float ema_t = NAN;
      float ema_h = NAN;
      float ema_p = NAN;
    };

    enum : uint8_t { PHASE_IDLE = 0, PHASE_WAIT_AHT = 1 };

    void readPrefs();
    void sampleStart();
    void sampleFinish();
    void emaUpdate(float t, float h, float p);
    bool tryDetect();

    uint8_t  _sda = ENV_DEFAULT_SDA;
    uint8_t  _scl = ENV_DEFAULT_SCL;
    uint8_t  _unit = UNIT_C;
    int8_t   _temp_offset_tenths = 0;
    uint16_t _altitude_m = 0;
    bool     _started = false;
    uint8_t  _drv_flags = 0;
    uint8_t  _detect_tries = 0;
    uint8_t  _phase = PHASE_IDLE;
    // Scratch between sampleStart and sampleFinish. Only valid in WAIT_AHT.
    float    _bt = NAN;
    float    _bh = NAN;
    float    _bp = NAN;
    BoschTHP::Sensor _bosch;
    AsairAHT::Sensor _aht;
    State*   _st = nullptr;

    float    _r_t = NAN;
    float    _r_h = NAN;
    float    _r_p = NAN;
    uint32_t _r_last_ms = 0;
};

#else  // DISABLE_ENVSENSOR

// Heap-regression / no-env build stub. No module registration, no library
// pull-in, no I2C probe. All accessors return constants so callers'
// `if (envsensor.present())` branches dead-code-eliminate at link time.
class EnvSensor {
  public:
    enum Unit : uint8_t { UNIT_C = 0, UNIT_F = 1, UNIT_K = 2 };
    bool    present() const   { return false; }
    bool    localPresent() const { return false; }
    bool    remoteFresh() const  { return false; }
    uint8_t unit() const      { return UNIT_C; }
    float   tempC() const     { return NAN; }
    float   humidity() const  { return NAN; }
    float   pressure() const  { return NAN; }
    float   tempUser() const  { return NAN; }
    void    setRemote(float, float, float) {}
};

#endif  // DISABLE_ENVSENSOR

extern EnvSensor envsensor;

#endif
