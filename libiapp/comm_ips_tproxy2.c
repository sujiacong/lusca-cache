#include "../include/config.h"

#if LINUX_TPROXY

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv4/ip_tproxy.h>

#include "../libstat/StatHist.h"

#include "../libsqinet/sqinet.h"
#include "../libsqdebug/debug.h"

#include "fd_types.h"
#include "comm_types.h"
#include "globals.h"

#ifdef _SQUID_LINUX_
#if HAVE_SYS_CAPABILITY_H
#undef _POSIX_SOURCE
/* Ugly glue to get around linux header madness colliding with glibc */
#define _LINUX_TYPES_H
#define _LINUX_FS_H
typedef uint32_t __u32;
#include <sys/capability.h>
#endif
#endif

#if HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include "../include/util.h"

/*
 * TPROXY2 listen() sockets just bind() to the specified address and receive connections that way.
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
	if (sqinet_get_family(a) != AF_INET)
		return COMM_ERROR;

	struct in_tproxy itp;

        itp.v.addr.faddr = sqinet_get_v4_inaddr(a,SQADDR_ASSERT_IS_V4);
	/* XXX the "port" is 0 here - so tproxy2 selects an outbound port rather than also spoofing the port */
        itp.v.addr.fport = 0;
        
        /* If these syscalls fail then we just fallback to connecting
         * normally by simply ignoring the errors...
         */
        itp.op = TPROXY_ASSIGN;
        if (setsockopt(fd, SOL_IP, IP_TPROXY, &itp, sizeof(itp)) == -1) {
            debug(20, 1) ("tproxy ip=%s,0x%x,port=%d ERROR ASSIGN\n",
               inet_ntoa(itp.v.addr.faddr),
               itp.v.addr.faddr.s_addr,
               itp.v.addr.fport);
	    return COMM_ERROR;
        }
        itp.op = TPROXY_FLAGS;
        itp.v.flags = ITP_CONNECT;
        if (setsockopt(fd, SOL_IP, IP_TPROXY, &itp, sizeof(itp)) == -1) {
           debug(20, 1) ("tproxy ip=%x,port=%d ERROR CONNECT\n",
               itp.v.addr.faddr.s_addr,
               itp.v.addr.fport);
           return COMM_ERROR;
        }
	return COMM_OK;
}

void
comm_ips_keepCapabilities(void)
{
#if HAVE_PRCTL && defined(PR_SET_KEEPCAPS) && HAVE_SYS_CAPABILITY_H
    if (prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0)) {
	/* Silent failure unless TPROXY is required. Maybe not started as root */
	if (need_linux_tproxy) {
		debug(1, 1) ("Error - Linux tproxy support requires capability setting which has failed.  Continuing without tproxy support\n");
		need_linux_tproxy = 0;
	}
    }
#endif
}

void
comm_ips_restoreCapabilities(int keep)
{
#if defined(_SQUID_LINUX_) && HAVE_SYS_CAPABILITY_H
    cap_user_header_t head = (cap_user_header_t) xcalloc(1, sizeof(cap_user_header_t));
    cap_user_data_t cap = (cap_user_data_t) xcalloc(1, sizeof(cap_user_data_t));

    head->version = _LINUX_CAPABILITY_VERSION;
    if (capget(head, cap) != 0) {
	debug(50, 1) ("Can't get current capabilities\n");
	goto nocap;
    }
    if (head->version != _LINUX_CAPABILITY_VERSION) {
	debug(50, 1) ("Invalid capability version %d (expected %d)\n", head->version, _LINUX_CAPABILITY_VERSION);
	goto nocap;
    }
    head->pid = 0;

    cap->inheritable = 0;
    cap->effective = (1 << CAP_NET_BIND_SERVICE);
    if (need_linux_tproxy)
	cap->effective |= (1 << CAP_NET_ADMIN) | (1 << CAP_NET_BROADCAST);
    if (!keep)
	cap->permitted &= cap->effective;
    if (capset(head, cap) != 0) {
	/* Silent failure unless TPROXY is required */
	if (need_linux_tproxy)
	    debug(50, 1) ("Error enabling needed capabilities. Will continue without tproxy support\n");
	need_linux_tproxy = 0;
    }
  nocap:
    xfree(head);
    xfree(cap);
#else
    if (need_linux_tproxy)
	debug(50, 1) ("Missing needed capability support. Will continue without tproxy support\n");
    need_linux_tproxy = 0;
#endif
}

#endif /* LINUX_TPROXY */
