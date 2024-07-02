
/*
 * Only compile this in if the other modules aren't included
 */

#include "../include/config.h"

#if FREEBSD_TPROXY

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "../libstat/StatHist.h"
#include "../libsqinet/sqinet.h"
#include "fd_types.h"
#include "comm_types.h"
#include "globals.h"

/* IP_NONLOCALOK = old-patch; IP_BINDANY = -current and 8.x */
#ifndef	IP_NONLOCALOK
#define		IP_NONLOCALOK	IP_BINDANY
#endif

/*
 * FreeBSD non-local bind listen() sockets bind to the relevant address and use
 * getsockname() to determine the original destination (local address being spoofed)
 * of the new connection.
 */
int
comm_ips_bind_lcl(int fd, sqaddr_t *a)
{
	if (bind(fd, sqinet_get_entry(a), sqinet_get_length(a)) != 0)
		return COMM_ERROR;
	return COMM_OK;
}

int
comm_ips_bind_rem(int fd, sqaddr_t *a)
{
    int on = 1;

    if (setsockopt(fd, IPPROTO_IP, IP_NONLOCALOK, (char *)&on, sizeof(on)) != 0)
        return COMM_ERROR;
    if (bind(fd, sqinet_get_entry(a), sqinet_get_length(a)) != 0)
        return COMM_ERROR;
    return COMM_OK;
}

void
comm_ips_keepCapabilities(void)
{
}

void
comm_ips_restoreCapabilities(int keep)
{
}

#endif
