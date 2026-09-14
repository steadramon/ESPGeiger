// lwIP options for the host build. Values the client's behaviour depends on
// must match the firmware build: pio run -e espgeigerhw -t idedata

#ifndef FAKE_LWIPOPTS_H
#define FAKE_LWIPOPTS_H

// Device build flags, verbatim.
#ifndef TCP_MSS
#define TCP_MSS 536
#endif
#define LWIP_IPV6      0
#define LWIP_FEATURES  1

// The client compares tcp_sndbuf() against TCP_MSS.
#define TCP_SND_BUF    (4 * TCP_MSS)

#define LWIP_TCP       1
#define LWIP_DNS       1
#define LWIP_CALLBACK_API 1

// No stack is driven here, so nothing needs an OS or a netif.
#define NO_SYS         1
#define LWIP_NETCONN   0
#define LWIP_SOCKET    0

// Off: would pull in checksum and pbuf machinery the fake lacks.

#define LWIP_ICMP      0
#define LWIP_IGMP      0
#define LWIP_DHCP      0
#define LWIP_AUTOIP    0
#define LWIP_UDP       0
#define LWIP_RAW       0
#define LWIP_STATS     0

#endif
