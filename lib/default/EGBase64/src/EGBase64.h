/*
  EGBase64.h - Buffer-in / buffer-out base64 encode + decode.

  Adapted from Densaugeo/base64_arduino:
    https://github.com/Densaugeo/base64_arduino/blob/d641350cfd2c3692a93969bfc324c3ce584cdebe/src/base64.hpp

  Renamed from <base64.h> to avoid colliding with the ESP8266/ESP32 core's
  <base64.h>, which is a different `class base64 { ... }` (heap-allocating
  String API) used internally by ESP8266WebServer. This is the no-heap
  variant — encode/decode in place into caller-provided buffers.

  Function bodies live in EGBase64.cpp so multiple TUs can include this
  header without the linker complaining about duplicate symbols.
*/

#ifndef EGBASE64_H_INCLUDED
#define EGBASE64_H_INCLUDED

unsigned char binary_to_base64(unsigned char v);
unsigned char base64_to_binary(unsigned char c);

unsigned int encode_base64_length(unsigned int input_length);

unsigned int decode_base64_length(unsigned char input[]);
unsigned int decode_base64_length(unsigned char input[], unsigned int input_length);

unsigned int encode_base64(unsigned char input[], unsigned int input_length, unsigned char output[]);

unsigned int decode_base64(unsigned char input[], unsigned char output[]);
unsigned int decode_base64(unsigned char input[], unsigned int input_length, unsigned char output[]);

#endif
