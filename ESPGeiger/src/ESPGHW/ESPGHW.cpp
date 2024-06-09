/*
  ESPGHW.cpp - Geiger Counter class
  
  Copyright (C) 2023 @steadramon

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
#ifdef ESPGEIGER_HW

#include <Arduino.h>
#include "ESPGHW.h"
#include "../Logger/Logger.h"
#include <FS.h>
#include <LittleFS.h>
#include "ArduinoJson.h"

ESPGeigerHW::ESPGeigerHW() {

}

void ESPGeigerHW::begin() {
  Log::console(PSTR("ESPG-HW: PWM Setup"));
  loadconfig();
#ifdef ESP8266
  pinMode(GEIGER_PWMPIN, OUTPUT);
  analogWrite (GEIGER_PWMPIN, 0) ;
  analogWriteFreq(_hw_freq);
#else
  ledcSetup(0, _hw_freq, 8);
  ledcAttachPin(GEIGER_PWMPIN, 0);
#endif
  status.hvReading.begin(SMOOTHED_AVERAGE, 3);
}

void ESPGeigerHW::loop() {
  if (_cur_duty != _hw_duty) {
#ifdef ESP8266
    analogWrite (GEIGER_PWMPIN, _hw_duty) ;
#else
    ledcWrite(0, _hw_duty);
#endif
    _cur_duty = _hw_duty;
  }
  int sensorValue = analogRead(GEIGER_VFEEDBACKPIN);
  float volts = ((0.0009765625032 * sensorValue)*_hw_vd_ratio)+_hw_vd_offset;
  status.hvReading.add((int)volts);
}

void ESPGeigerHW::fiveloop() {
  int hvolts = (int)status.hvReading.get();
  Log::console(PSTR("ESPG-HW: HV: %d"), hvolts);
}

void ESPGeigerHW::saveconfig() {

  Log::console(PSTR("ESPG-HW: Saving ..."));
  LittleFS.begin();

  // Open the file
  File configFile = LittleFS.open("/espgeigerhw.json", "w");
  if (!configFile)
  {
    Log::console(PSTR("ESPG-HW: Failed to open espgeigerhw.json"));
    return;
  }
  char jsonBuffer[128] = "";

  int total = sizeof(jsonBuffer);
  char freq[16];
  itoa(_hw_freq, freq, 10); 
  char duty[16];
  itoa(_hw_duty, duty, 10); 
  char ratio[16];
  itoa(_hw_vd_ratio, ratio, 10); 
  char offset[16];
  itoa(_hw_vd_offset, offset, 10);

  sprintf_P (
    jsonBuffer,
    PSTR("{\"freq\":\"%s\",\"duty\":\"%s\",\"ratio\":\"%s\",\"offset\":\"%s\"}"),
    freq,
    duty,
    ratio,
    offset
  );
  configFile.print(jsonBuffer);

  // Close the file
  configFile.close();
  LittleFS.end();
  Log::console(PSTR("ESPG-HW: Config Saved"));
}

void ESPGeigerHW::loadconfig() {
  LittleFS.begin();
  Log::console(PSTR("ESPG-HW: Loading ..."));
  if (LittleFS.exists("/espgeigerhw.json"))
  {
    File configFile = LittleFS.open("/espgeigerhw.json", "r");
    if (configFile)
    {
      // Process the json data
      DynamicJsonDocument jsonBuffer(128);
      DeserializationError error = deserializeJson(jsonBuffer, configFile);
      if (!error)
      {
        const char* freq = jsonBuffer["freq"];
        if (freq) {
          set_freq(atoi(freq));
        }
        const char* duty = jsonBuffer["duty"];
        if (duty) {
          set_duty(atoi(duty));
        }
        const char* ratio = jsonBuffer["ratio"];
        if (ratio) {
          set_vd_ratio(atoi(ratio));
        }
        const char* offset = jsonBuffer["offset"];
        if (offset) {
          set_vd_offset(atoi(offset));
        }
      }
      else {
        Log::console(PSTR("ESPG-HW: failed to load json params"));
      }
      // Close file
      configFile.close();
    }
    else
    {
      Log::console(PSTR("ESPG-HW: failed to open espgeigerhw.json file"));
    }
  }
  else
  {
    Log::console(PSTR("ESPG-HW: No espgeigerhw.json file, using defaults"));
  }
  LittleFS.end();
  Log::console(PSTR("ESPG-HW: freq: %d"), _hw_freq);
  Log::console(PSTR("ESPG-HW: duty: %d"), _hw_duty);
  Log::console(PSTR("ESPG-HW: VD Ratio: %d"), _hw_vd_ratio);
}
#endif