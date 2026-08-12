// lwIP platform layer for the host build.
//
// Replaces the ESP8266 SDK's arch/cc.h, which pulls in the SDK headers. lwIP
// needs the integer types, byte order, printf specifiers and the diagnostic
// hooks; nothing here is target-specific because the fake never touches a
// netif or a real packet.

#ifndef FAKE_ARCH_CC_H
#define FAKE_ARCH_CC_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

typedef uint8_t   u8_t;
typedef int8_t    s8_t;
typedef uint16_t  u16_t;
typedef int16_t   s16_t;
typedef uint32_t  u32_t;
typedef int32_t   s32_t;
typedef uintptr_t mem_ptr_t;

#define U16_F PRIu16
#define S16_F PRId16
#define X16_F PRIx16
#define U32_F PRIu32
#define S32_F PRId32
#define X32_F PRIx32
#define SZT_F "zu"

#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_STRUCT   __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

#define LWIP_PLATFORM_DIAG(x)   do { printf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) \
  do { printf("lwip assert \"%s\" at %s:%d\n", x, __FILE__, __LINE__); abort(); } while (0)

// lwIP's own htons/ntohs would clash with the host's <sys/_endian.h>.
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS

#endif
