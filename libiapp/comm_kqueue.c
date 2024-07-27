
/*
 * $Id: comm_kqueue.c 14748 2010-08-06 06:40:51Z adrian.chadd $
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
#include <sys/types.h>
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
#include "../libsqinet/sqinet.h"

#include "iapp_ssl.h"
#include "globals.h"
#include "fd_types.h"
#include "comm_types.h"
#include "comm.h"

#include "comm_generic.c"

#if HAVE_SYS_EVENT_H
#include <sys/event.h>
#endif

#define KE_QUEUE_STEP 128
#define STATE_READ      1
#define STATE_WRITE     2

static int kq;
static struct timespec zero_timespec;
static struct kevent *kqlst;	/* change buffer */
static struct kevent *ke;	/* event buffer */
static int kqmax;		/* max structs to buffer */
static int kqoff;		/* offset into the buffer */
static unsigned char *kqueue_state;	/* keep track of the kqueue state */

static void
do_select_init()
{
    kq = kqueue();
    assert(kq >= 0);
#if 0
    if (kq < 0)
	fatalf("comm_select_init: kqueue(): %s\n", xstrerror());
#endif
    fd_open(kq, FD_UNKNOWN, "kqueue ctl");
    commSetCloseOnExec(kq);
    kqmax = KE_QUEUE_STEP;
    kqlst = xmalloc(sizeof(*kqlst) * kqmax);
    ke = xmalloc(sizeof(*ke) * kqmax);
    kqueue_state = xcalloc(Squid_MaxFD, sizeof(*kqueue_state));
    zero_timespec.tv_sec = 0;
    zero_timespec.tv_nsec = 0;
}

void
comm_select_postinit()
{
    debugs(5, 1, "Using kqueue for the IO loop");
}

static void
do_select_shutdown()
{
    fd_close(kq);
    close(kq);
    kq = -1;
    safe_free(kqueue_state);
}

const char *
comm_select_status(void)
{
    return("kqueue");
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
    struct kevent *kep;
    int st_new = (need_read ? STATE_READ : 0) |
    (need_write ? STATE_WRITE : 0);
    int st_change;

    assert(fd >= 0);
    debugs(5, 8, "commSetEvents(fd=%d, read=%d, write=%d)", fd, need_read, need_write);

    st_change = kqueue_state[fd] ^ st_new;
    if (!st_change)
	return;

    if (kqoff >= kqmax - 2) {
	kqmax = kqmax + KE_QUEUE_STEP;
	assert(kqmax < Squid_MaxFD * 4);
	kqlst = xrealloc(kqlst, sizeof(*kqlst) * kqmax);
	ke = xrealloc(ke, sizeof(*ke) * kqmax);
    }
    kep = kqlst + kqoff;

    if (st_change & STATE_READ) {
	EV_SET(kep, (uintptr_t) fd, EVFILT_READ,
	    need_read ? EV_ADD : EV_DELETE, 0, 0, 0);
	kqoff++;
	kep++;
    }
    if (st_change & STATE_WRITE) {
	EV_SET(kep, (uintptr_t) fd, EVFILT_WRITE,
	    need_write ? EV_ADD : EV_DELETE, 0, 0, 0);
	kqoff++;
	kep++;
    }
    kqueue_state[fd] = st_new;
}

static int
do_comm_select(int msec)
{
    int i;
    int num, saved_errno;
    struct timespec timeout;

    timeout.tv_sec = msec / 1000;
    timeout.tv_nsec = (msec % 1000) * 1000000;

    CommStats.syscalls.polls++;
    num = kevent(kq, kqlst, kqoff, ke, kqmax, &timeout);
    saved_errno = errno;
    getCurrentTime();
    debugs(5, 5, "do_comm_select: %d fds ready", num);
    kqoff = 0;
    if (num < 0) {
	if (ignoreErrno(saved_errno))
	    return COMM_OK;

	debugs(5, 1, "comm_select: kevent failure: %s", xstrerror());
	return COMM_ERROR;
    }
    statHistCount(&select_fds_hist, num);
    if (num == 0)
	return COMM_TIMEOUT;

    for (i = 0; i < num; i++) {
	int fd = (int) ke[i].ident;
	if (ke[i].flags & EV_ERROR) {
	    errno = ke[i].data;
	    debugs(5, 3, "do_comm_select: kqueue event error: %s",
		xstrerror());
	    continue;		/* XXX! */
	}
	switch (ke[i].filter) {
	case EVFILT_READ:
	    comm_call_handlers(fd, 1, 0);
	    break;
	case EVFILT_WRITE:
	    comm_call_handlers(fd, 0, 1);
	    break;
	default:
	    debugs(5, 1, "comm_select: unexpected event: %d",
		ke[i].filter);
	    break;
	}
    }

    if (num >= kqmax) {
	kqmax = kqmax + KE_QUEUE_STEP;
	kqlst = xrealloc(kqlst, sizeof(*kqlst) * kqmax);
	ke = xrealloc(ke, sizeof(*ke) * kqmax);
    }
    return COMM_OK;
}
