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

#ifndef DISABLE_ENVSENSOR
#include <BoschTHP.h>
#include <AsairAHT.h>
#include "Corium/Measure/Measure.h"
#include "Corium/Module/Module.h"
#include "Corium/System/FastMillis.h"
#endif

// sample counts are at ENV_SAMPLE_MS
#define ENV_SAMPLE_MS    10000UL
#define ENV_EMA_ALPHA     0.1f
#define ENV_STUCK_SAMPLES 180   // 30 min
#define ENV_FAIL_SAMPLES  6     // 1 min
#define ENV_RETRY_MS      60000UL   // max 0xFFFF
#define ENV_AHT_WAIT_MS   80
// BME280, BMP280 and AHTxx datasheets
#define ENV_T_MIN   -40.0f
#define ENV_T_MAX    85.0f
#define ENV_H_MIN     0.0f
#define ENV_H_MAX   100.0f
#define ENV_P_MIN   300.0f
#define ENV_P_MAX  1100.0f

#ifndef DISABLE_ENVSENSOR

class EnvSensor : public corium::Module {
  public:
    enum Unit : uint8_t { UNIT_C = 0, UNIT_F = 1, UNIT_K = 2 };

    EnvSensor() {}
    const char* name() override { return PSTR("env"); }
    uint8_t priority() override { return CORIUM_PRIORITY_DEFAULT; }
    uint8_t displayOrder() override { return 13; }   // after Input (12)
    void begin() override;
    void loop(unsigned long now) override;
    const corium::PrefGroup* prefsGroup() override;
    void onPrefsLoaded() override;
    bool hwPresent() override { return present(); }
    corium::MeasureRows measurements() override;

    static constexpr uint8_t DRV_BOSCH = 1 << 0;
    static constexpr uint8_t DRV_AHT   = 1 << 1;

    bool    present() const { return localPresent() || remoteFresh(); }
    bool    localPresent() const { return _drv_flags != 0 && _st != nullptr; }
    const __FlashStringHelper* chipName() const;

    // local sensor, else UdpRx's; NAN = none
    float tempC() const;      // C, offset applied
    float humidity() const;   // %
    float pressure() const;   // hPa, sea level once an altitude is set

    // UdpRx's /env rows, used with no local sensor
    bool remoteFresh() const;

  private:
    struct State {
      float ema_t = NAN;
      float ema_h = NAN;
      float ema_p = NAN;
      float prev_t = NAN;
      float prev_h = NAN;
      float prev_p = NAN;
      uint16_t stuck = 0;
      uint8_t  fails = 0;
    };

    enum : uint8_t { PHASE_IDLE = 0, PHASE_WAIT_AHT = 1 };

    void readPrefs();
    void sampleStart();
    void sampleFinish();
    void sampleDone();
    void emaUpdate(float t, float h, float p);
    bool tryDetect();

    int8_t   _temp_x10 = 0;
    uint16_t _altitude_m = 0;
    bool     _started = false;
    uint8_t  _drv_flags = 0;
    uint8_t  _detect_tries = 0;
    uint8_t  _phase = PHASE_IDLE;
    // WAIT_AHT only
    float    _t = NAN;
    float    _h = NAN;
    float    _p = NAN;
    bool     _failed = false;
    BoschTHP::Sensor _bosch;
    AsairAHT::Sensor _aht;
    State*   _st = nullptr;
};

#else  // DISABLE_ENVSENSOR

// No-env stub
class EnvSensor {
  public:
    enum Unit : uint8_t { UNIT_C = 0, UNIT_F = 1, UNIT_K = 2 };
    bool    present() const   { return false; }
    bool    localPresent() const { return false; }
    bool    remoteFresh() const  { return false; }
    float   tempC() const     { return NAN; }
    float   humidity() const  { return NAN; }
    float   pressure() const  { return NAN; }
};

#endif  // DISABLE_ENVSENSOR

extern EnvSensor envsensor;

#endif
