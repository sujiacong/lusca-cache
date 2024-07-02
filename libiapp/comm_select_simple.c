
/*
 * $Id: comm_select_simple.c 13518 2009-01-06 21:40:17Z adrian.chadd $
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
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <err.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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

#include "iapp_ssl.h"
#include "globals.h"
#include "fd_types.h"
#include "comm_types.h"
#include "comm.h"

#include "comm_generic.c"

#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#if HAVE_SYS_SELECT_H
#include <sys/select.h>
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
    debug(5, 1) ("Using select in POSIX mode for the IO loop\n");
}

static void
do_select_shutdown()
{
}

const char *
comm_select_status(void)
{
    return("select in POSIX mode");
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
    if (need_read && !FD_ISSET(fd, &global_readfds)) {
	FD_SET(fd, &global_readfds);
	nreadfds++;
    } else if (!need_read && FD_ISSET(fd, &global_readfds)) {
	FD_CLR(fd, &global_readfds);
	nreadfds--;
    }
    if (need_write && !FD_ISSET(fd, &global_writefds)) {
	FD_SET(fd, &global_writefds);
	nwritefds++;
    } else if (!need_write && FD_ISSET(fd, &global_writefds)) {
	FD_CLR(fd, &global_writefds);
	nwritefds--;
    }
}

static int
do_comm_select(int msec)
{
    int num;
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
    statCounter.syscalls.selects++;
    num = select(Biggest_FD + 1, &readfds, &writefds, &errfds, &tv);

    if (num < 0) {
	getCurrentTime();
	if (ignoreErrno(errno))
	    return COMM_OK;

	debug(5, 1) ("comm_select: select failure: %s\n", xstrerror());
	return COMM_ERROR;
    }
    statHistCount(&statCounter.select_fds_hist, num);

    if (num == 0)
	return COMM_TIMEOUT;

    for (fd = 0; fd <= Biggest_FD; fd++) {
	int read_event = FD_ISSET(fd, &readfds);
	int write_event = FD_ISSET(fd, &writefds) || FD_ISSET(fd, &errfds);
	if (read_event || write_event)
	    comm_call_handlers(fd, read_event, write_event);
    }
    return COMM_OK;
}
