/*
  EGBase64.cpp - Based on Densaugeo/base64_arduino, see EGBase64.h
*/

#include "EGBase64.h"

unsigned char binary_to_base64(unsigned char v) {
  if (v < 26) return v + 'A';
  if (v < 52) return v + 71;
  if (v < 62) return v - 4;
  if (v == 62) return '+';
  if (v == 63) return '/';
  return 64;
}

unsigned char base64_to_binary(unsigned char c) {
  if ('A' <= c && c <= 'Z') return c - 'A';
  if ('a' <= c && c <= 'z') return c - 71;
  if ('0' <= c && c <= '9') return c + 4;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return 255;
}

unsigned int encode_base64_length(unsigned int input_length) {
  return EGB64_LEN(input_length);
}

unsigned int decode_base64_length(const unsigned char input[], unsigned int input_length) {
  const unsigned char *start = input;
  // bounds first, no sentinel
  while ((unsigned int)(input - start) < input_length && base64_to_binary(input[0]) < 64) {
    ++input;
  }
  input_length = input - start;
  return input_length / 4 * 3 + (input_length % 4 ? input_length % 4 - 1 : 0);
}

unsigned int encode_base64(const unsigned char input[], unsigned int input_length, unsigned char output[]) {
  unsigned int full_sets = input_length / 3;
  for (unsigned int i = 0; i < full_sets; ++i) {
    output[0] = binary_to_base64(                         input[0] >> 2);
    output[1] = binary_to_base64((input[0] & 0x03) << 4 | input[1] >> 4);
    output[2] = binary_to_base64((input[1] & 0x0F) << 2 | input[2] >> 6);
    output[3] = binary_to_base64( input[2] & 0x3F);
    input  += 3;
    output += 4;
  }
  switch (input_length % 3) {
    case 0:
      output[0] = '\0';
      break;
    case 1:
      output[0] = binary_to_base64(                         input[0] >> 2);
      output[1] = binary_to_base64((input[0] & 0x03) << 4);
      output[2] = '=';
      output[3] = '=';
      output[4] = '\0';
      break;
    case 2:
      output[0] = binary_to_base64(                         input[0] >> 2);
      output[1] = binary_to_base64((input[0] & 0x03) << 4 | input[1] >> 4);
      output[2] = binary_to_base64((input[1] & 0x0F) << 2);
      output[3] = '=';
      output[4] = '\0';
      break;
  }
  return encode_base64_length(input_length);
}

unsigned int decode_base64(const unsigned char input[], unsigned int input_length, unsigned char output[]) {
  unsigned int output_length = decode_base64_length(input, input_length);
  for (unsigned int i = 2; i < output_length; i += 3) {
    output[0] = base64_to_binary(input[0]) << 2 | base64_to_binary(input[1]) >> 4;
    output[1] = base64_to_binary(input[1]) << 4 | base64_to_binary(input[2]) >> 2;
    output[2] = base64_to_binary(input[2]) << 6 | base64_to_binary(input[3]);
    input  += 4;
    output += 3;
  }
  switch (output_length % 3) {
    case 1:
      output[0] = base64_to_binary(input[0]) << 2 | base64_to_binary(input[1]) >> 4;
      break;
    case 2:
      output[0] = base64_to_binary(input[0]) << 2 | base64_to_binary(input[1]) >> 4;
      output[1] = base64_to_binary(input[1]) << 4 | base64_to_binary(input[2]) >> 2;
      break;
  }
  return output_length;
}
