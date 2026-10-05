/*
 * Stand-in for <sys/socket.h> as ESP-IDF v5.5.2 has it (components/lwip/port/esp32xx/include/sys/socket.h,
 * which is lwIP): the one call display/main/web.c makes on a connection. setsockopt() is an inline function
 * there (components/lwip/include/lwip/sockets.h) that calls lwip_setsockopt(); the two numbers are those of
 * lwip/sockets.h in the lwIP of that version (espressif/esp-lwip, commit fd432e4e).
 *
 * This file takes the place of the header of the PC for the simulations: they open no connection.
 */
#ifndef __SIM_SYS_SOCKET_H__
#define __SIM_SYS_SOCKET_H__

#include <stdint.h>

#define IPPROTO_TCP     6
#define TCP_NODELAY     0x01

int lwip_setsockopt(int s, int level, int optname, const void *optval, uint32_t optlen);

static inline int setsockopt(int s, int level, int optname, const void *opval, uint32_t optlen)
{
	return lwip_setsockopt(s, level, optname, opval, optlen);
}

#endif
