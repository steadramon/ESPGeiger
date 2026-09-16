/*
  EGBase64.h - Buffer-in / buffer-out base64 encode + decode.

  Adapted from Densaugeo/base64_arduino (MIT, (c) 2016 Densaugeo; see LICENSE).
  ESPGeiger changes are GPL-3.0.
    https://github.com/Densaugeo/base64_arduino/blob/d641350cfd2c3692a93969bfc324c3ce584cdebe/src/base64.hpp

  Not <base64.h>, that clashes with the core's heap one
*/

#ifndef EGBASE64_H_INCLUDED
#define EGBASE64_H_INCLUDED

// for array sizes, no NUL
#define EGB64_LEN(n) (((n) + 2) / 3 * 4)

unsigned char binary_to_base64(unsigned char v);
unsigned char base64_to_binary(unsigned char c);

unsigned int encode_base64_length(unsigned int input_length);

unsigned int decode_base64_length(const unsigned char input[], unsigned int input_length);

// writes a NUL too, output needs EGB64_LEN() + 1
unsigned int encode_base64(const unsigned char input[], unsigned int input_length, unsigned char output[]);

unsigned int decode_base64(const unsigned char input[], unsigned int input_length, unsigned char output[]);

#endif
