/*
  EGBase64.h - Buffer-in / buffer-out base64 encode + decode.

  Adapted from Densaugeo/base64_arduino (MIT, (c) 2016 Densaugeo; see LICENSE).
  ESPGeiger changes are GPL-3.0.
    https://github.com/Densaugeo/base64_arduino/blob/d641350cfd2c3692a93969bfc324c3ce584cdebe/src/base64.hpp

  Renamed from <base64.h> to avoid colliding with the ESP8266/ESP32 core's
  <base64.h>, which is a different `class base64 { ... }` (heap-allocating
  String API) used internally by ESP8266WebServer. This is the no-heap
  variant - encode/decode in place into caller-provided buffers.

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

// CONTRACT: output buffer must be at least `encode_base64_length(input_length) + 1`
// bytes. The function writes the 4*ceil(N/3) encoded chars AND a trailing NUL
// terminator (the +1 byte). Streaming callers that write successive encodes
// into a single rolling buffer must reserve room for the NUL or encode into a
// scratch buffer first - the NUL OOBs into the next encode's start position.
unsigned int encode_base64(unsigned char input[], unsigned int input_length, unsigned char output[]);

unsigned int decode_base64(unsigned char input[], unsigned char output[]);
unsigned int decode_base64(unsigned char input[], unsigned int input_length, unsigned char output[]);

#endif
