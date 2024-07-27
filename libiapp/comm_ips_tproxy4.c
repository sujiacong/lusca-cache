
/*
 * Linux TPROXY4 support
 */

#include "../include/config.h"

#if LINUX_TPROXY4

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <limits.h>

#include <linux/netfilter_ipv4.h>

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

#include <linux/capability.h>

#if HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

/* Just in case the IP_TRANSPARENT define isn't included somehow */
#if !defined(IP_TRANSPARENT)
#define IP_TRANSPARENT 19
#endif

#include "../include/util.h"

#include "../libcore/tools.h"
#include "../libcore/varargs.h"
#include "../libcore/debug.h"

#include "../libstat/StatHist.h"
#include "../libsqinet/sqinet.h"

#include "fd_types.h"
#include "comm_types.h"
#include "globals.h"

static int
tproxy4_set_transparent(int fd, sqaddr_t *a)
{
    int on = 1;

    if (setsockopt(fd, SOL_IP, IP_TRANSPARENT, (char *)&on, sizeof(on)) != 0)
        return COMM_ERROR;
    if (bind(fd, sqinet_get_entry(a), sqinet_get_length(a)) != 0)
        return COMM_ERROR;
    return COMM_OK;
}

/*
 * TPROXY4 requires the local socket be set IP_TRANSPARENT and then the bind() address
 * will determine which TCP/UDP connections are hijacked.
 */
int
comm_ips_bind_lcl(int fd, sqaddr_t *a)
{
	return tproxy4_set_transparent(fd, a);
}

int
comm_ips_bind_rem(int fd, sqaddr_t *a)
{
	return tproxy4_set_transparent(fd, a);
}

void
comm_ips_keepCapabilities(void)
{
    if (prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0)) {
	/* Silent failure unless TPROXY is required. Maybe not started as root */
	if (need_linux_tproxy) {
		debugs(1, 1, "Error - Linux tproxy support requires capability setting which has failed.  Continuing without tproxy support");
		need_linux_tproxy = 0;
	}
    }
}

void
comm_ips_restoreCapabilities(int keep)
{
    cap_user_header_t head = (cap_user_header_t) xcalloc(1, sizeof(cap_user_header_t));
    cap_user_data_t cap = (cap_user_data_t) xcalloc(1, sizeof(cap_user_data_t));

    head->version = _LINUX_CAPABILITY_VERSION;
    if (capget(head, cap) != 0) {
	debugs(50, 1, "Can't get current capabilities");
	goto nocap;
    }
    if (head->version != _LINUX_CAPABILITY_VERSION) {
	debugs(50, 1, "Invalid capability version %d (expected %d)", head->version, _LINUX_CAPABILITY_VERSION);
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
	    debugs(50, 1, "Error enabling needed capabilities. Will continue without tproxy support");
	need_linux_tproxy = 0;
    }
  nocap:
    xfree(head);
    xfree(cap);
}

#endif /* LINUX_TPROXY4 */
