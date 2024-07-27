
/*
 * $Id: comm_select_win32.c 14566 2010-04-11 00:54:53Z adrian.chadd $
 *
 * DEBUG: section 5     Socket Functions
 *
 * SQUID Web Proxy Cache          http://www.squid-cache.org/
 * ----------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from
 *  the Internet community; see the CONTRIBUTORS file for full
 *  details.   Many organizations have provided support for Squid's
 *  development; see the SPONSORS file for full details.  Squid is
 *  Copyrighted (C) 2001 by the Regents of the University of
 *  California; see the COPYRIGHT file for full details.  Squid
 *  incorporates software developed and/or copyrighted by other
 *  sources; see the CREDITS file for full details.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 *
 */

#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#if HAVE_STRING_H
#include <string.h>
#endif
#include <math.h>
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif
#if HAVE_ERR_H
#include <err.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#include "../include/Array.h"
#include "../include/Stack.h"
#include "../include/util.h"
#include "../libcore/valgrind.h"
#include "../libcore/varargs.h"
#include "../libcore/debug.h"
#include "../libcore/kb.h"
#include "../libcore/gb.h"
#include "../libcore/tools.h"

#include "../libmem/MemPool.h"
#include "../libmem/MemBufs.h"
#include "../libmem/MemBuf.h"

#include "../libstat/StatHist.h"

#include "../libcb/cbdata.h"

#include "../libsqinet/sqinet.h"

#include "iapp_ssl.h"
#include "globals.h"
#include "fd_types.h"
#include "comm_types.h"
#include "comm.h"

#include "comm_generic.c"

#if HAVE_WINSOCK2_H
#include <Winsock2.h>
#endif


static fd_set global_readfds;
static fd_set global_writefds;
static int nreadfds;
static int nwritefds;

static void
do_select_init()
{
    if (Squid_MaxFD > FD_SETSIZE)
	Squid_MaxFD = FD_SETSIZE;
    nreadfds = nwritefds = 0;
}

void
comm_select_postinit()
{
    debugs(5, 1, "Using select for the IO loop");
}

static void
do_select_shutdown()
{
}

const char *
comm_select_status(void)
{
    return("select (win32)");
}

void
commOpen(int fd)
{
}

void
commClose(int fd)
{
    commSetEvents(fd, 0, 0);
}

void
commSetEvents(int fd, int need_read, int need_write)
{
    if (need_read && !__WSAFDIsSet(fd_table[fd].win32.handle, &global_readfds)) {
	FD_SET(fd, &global_readfds);
	nreadfds++;
    } else if (!need_read && __WSAFDIsSet(fd_table[fd].win32.handle, &global_readfds)) {
	FD_CLR(fd, &global_readfds);
	nreadfds--;
    }
    if (need_write && !__WSAFDIsSet(fd_table[fd].win32.handle, &global_writefds)) {
	FD_SET(fd, &global_writefds);
	nwritefds++;
    } else if (!need_write && __WSAFDIsSet(fd_table[fd].win32.handle, &global_writefds)) {
	FD_CLR(fd, &global_writefds);
	nwritefds--;
    }
}

static int
do_comm_select(int msec)
{
    int num, saved_errno;
    struct timeval tv;
    fd_set readfds;
    fd_set writefds;
    fd_set errfds;
    int fd;

    if (nreadfds + nwritefds == 0) {
	assert(shutting_down);
	return COMM_SHUTDOWN;
    }
    memcpy(&readfds, &global_readfds, sizeof(fd_set));
    memcpy(&writefds, &global_writefds, sizeof(fd_set));
    memcpy(&errfds, &global_writefds, sizeof(fd_set));
    tv.tv_sec = msec / 1000;
    tv.tv_usec = (msec % 1000) * 1000;
    CommStats.syscalls.selects++;
    num = select(Biggest_FD + 1, &readfds, &writefds, &errfds, &tv);
    saved_errno = errno;
    getCurrentTime();
    debugs(5, 5, "do_comm_select: %d fds ready", num);
    if (num < 0) {
	if (ignoreErrno(saved_errno))
	    return COMM_OK;

	debugs(5, 1, "comm_select: select failure: %s", xstrerror());
	return COMM_ERROR;
    }
    CommStats.select_fds++;

    if (num == 0)
	return COMM_TIMEOUT;

    for (fd = 0; fd <= Biggest_FD; fd++) {
	int read_event = __WSAFDIsSet(fd_table[fd].win32.handle, &readfds);
	int write_event = __WSAFDIsSet(fd_table[fd].win32.handle, &writefds) || __WSAFDIsSet(fd_table[fd].win32.handle, &errfds);
	if (read_event || write_event)
	    comm_call_handlers(fd, read_event, write_event);
    }
    return COMM_OK;
}
