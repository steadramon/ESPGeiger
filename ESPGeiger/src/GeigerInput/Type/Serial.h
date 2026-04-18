/*
  GeigerInput/Type/Serial.h - Class for Serial type counter

  Copyright (C) 2024 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/
#ifndef GEIGERTESTSRL_H
#define GEIGERTESTSRL_H
#include <Arduino.h>
#include <SoftwareSerial.h>
#include "../GeigerInput.h"

#ifndef GEIGER_TXPIN
#define GEIGER_TXPIN -1
#endif

class GeigerSerial : public GeigerInput
{
  public:
    GeigerSerial();
    void begin();
    void loop();
    void secondTicker();
  private:
    void pullSerial();
    char _serial_buffer[64];
    uint8_t _serial_idx = 0;
    void handleSerial(char* input);
    float partial_clicks = 0;
    int serial_value = 0;
    unsigned long last_serial = 0;
    int avg_diff = 0;
    uint8_t _loop_c = 0;
    uint8_t _serial_type = GEIGER_SERIALTYPE;
};
#endif
