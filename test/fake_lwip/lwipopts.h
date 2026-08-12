// lwIP options for the host build.
//
// lwip/opt.h includes "lwipopts.h" off the include path and supplies a default
// for everything not set here. This file takes the place of the ESP8266 SDK's
// own lwipopts.h, which is 118 KB and reaches into osapi.h, ets_sys.h and the
// rest of the SDK.
//
// Values that reach the client's behaviour are matched to the firmware build
// and must stay matched. Read them back with:
//   pio run -e espgeigerhw -t idedata

#ifndef FAKE_LWIPOPTS_H
#define FAKE_LWIPOPTS_H

// Device build flags, verbatim.
#ifndef TCP_MSS
#define TCP_MSS 536
#endif
#define LWIP_IPV6      0
#define LWIP_FEATURES  1

// The client reads tcp_sndbuf() and compares against TCP_MSS when deciding
// how much of a write it can take.
#define TCP_SND_BUF    (4 * TCP_MSS)

#define LWIP_TCP       1
#define LWIP_DNS       1
#define LWIP_CALLBACK_API 1

// No stack is driven here, so nothing needs an OS or a netif.
#define NO_SYS         1
#define LWIP_NETCONN   0
#define LWIP_SOCKET    0

// Off: pulls in the checksum and pbuf machinery the fake does not implement.
#define LWIP_ICMP      0
#define LWIP_IGMP      0
#define LWIP_DHCP      0
#define LWIP_AUTOIP    0
#define LWIP_UDP       0
#define LWIP_RAW       0
#define LWIP_STATS     0

#endif
