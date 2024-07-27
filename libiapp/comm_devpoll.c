
/*
 * $Id: comm_devpoll.c 14125 2009-07-05 12:26:13Z adrian.chadd $
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

#include <sys/devpoll.h>

#define	DEVPOLL_UPDATESIZE	OPEN_MAX
#define	DEVPOLL_QUERYSIZE	OPEN_MAX

static int devpoll_fd;
static struct timespec zero_timespec;

/*
 * This is a very simple driver for Solaris /dev/poll.
 *
 * The updates are batched, one trip through the comm loop.
 * (like libevent.) We keep a pointer into the structs so we
 * can zero out an entry in the poll list if its active.
 */

/* Current state */
struct _devpoll_state {
    char state;
    int update_offset;
};

/* The update list */
struct {
    struct pollfd *pfds;
    int cur;
    int size;
} devpoll_update;

static struct _devpoll_state *devpoll_state;
static struct dvpoll do_poll;
static int dpoll_nfds;

static void
do_select_init()
{
    devpoll_fd = open("/dev/poll", O_RDWR);
    if (devpoll_fd < 0)
	fatalf("comm_select_init: can't open /dev/poll: %s\n", xstrerror());

    zero_timespec.tv_sec = 0;
    zero_timespec.tv_nsec = 0;

    /* This tracks the FD devpoll offset+state */
    devpoll_state = xcalloc(Squid_MaxFD, sizeof(struct _devpoll_state));

    /* And this is the stuff we use to read events */
    do_poll.dp_fds = xcalloc(DEVPOLL_QUERYSIZE, sizeof(struct pollfd));
    dpoll_nfds = DEVPOLL_QUERYSIZE;

    devpoll_update.pfds = xcalloc(DEVPOLL_UPDATESIZE, sizeof(struct pollfd));
    devpoll_update.cur = -1;
    devpoll_update.size = DEVPOLL_UPDATESIZE;

    fd_open(devpoll_fd, FD_UNKNOWN, "devpoll ctl");
    commSetCloseOnExec(devpoll_fd);
}

static void
comm_flush_updates(void)
{
    int i;
    if (devpoll_update.cur == -1)
	return;

    debugs(5, 5, "comm_flush_updates: %d fds queued", devpoll_update.cur + 1);

    i = write(devpoll_fd, devpoll_update.pfds, (devpoll_update.cur + 1) * sizeof(struct pollfd));
    assert(i > 0);
    assert(i == sizeof(struct pollfd) * (devpoll_update.cur + 1));
    devpoll_update.cur = -1;
}

/*
 * We could be "optimal" and -change- an existing entry if they
 * just add a bit - since the devpoll interface OR's multiple fd
 * updates. We'll need to POLLREMOVE entries which has a bit cleared
 * but for now I'll do whats "easier" and add the smart logic
 * later.
 */
static void
comm_update_fd(int fd, int events)
{
    debugs(5, 5, "comm_update_fd: fd %d: events %d", fd, events);
    if (devpoll_update.cur != -1 && (devpoll_update.cur == devpoll_update.size))
	comm_flush_updates();
    devpoll_update.cur++;
    debugs(5, 5, "  -> new slot (%d)", devpoll_update.cur);
    devpoll_state[fd].update_offset = devpoll_update.cur;
    devpoll_update.pfds[devpoll_update.cur].fd = fd;
    devpoll_update.pfds[devpoll_update.cur].events = events;
    devpoll_update.pfds[devpoll_update.cur].revents = 0;
}

void
comm_select_postinit()
{
    debugs(5, 1, "Using /dev/poll for the IO loop");
}

static void
do_select_shutdown()
{
    fd_close(devpoll_fd);
    close(devpoll_fd);
    devpoll_fd = -1;
    xfree(devpoll_state);
}

const char *
comm_select_status(void)
{
    return("/dev/poll");
}

void
commOpen(int fd)
{
    debugs(5, 5, "commOpen: %d", fd);
    devpoll_state[fd].state = 0;
    devpoll_state[fd].update_offset = -1;
}

void
commClose(int fd)
{
    debugs(5, 5, "commClose: %d", fd);
    comm_update_fd(fd, POLLREMOVE);
}

void
commSetEvents(int fd, int need_read, int need_write)
{
    int st_new = (need_read ? POLLIN : 0) | (need_write ? POLLOUT : 0);
    int st_change;

    if (fd_table[fd].flags.closing)
	return;

    debugs(5, 5, "commSetEvents(fd=%d, read=%d, write=%d)", fd, need_read, need_write);

    st_change = devpoll_state[fd].state ^ st_new;
    if (!st_change)
	return;

    comm_update_fd(fd, POLLREMOVE);
    if (st_new)
	comm_update_fd(fd, st_new);
    devpoll_state[fd].state = st_new;
}

static int
do_comm_select(int msec)
{
    int i;
    int num, saved_errno;

    statCounter.syscalls.polls++;

    do_poll.dp_timeout = msec;
    do_poll.dp_nfds = dpoll_nfds;
    /* dp_fds is already allocated */

    debugs(5, 5, "do_comm_select: begin");
    comm_flush_updates();

    num = ioctl(devpoll_fd, DP_POLL, &do_poll);
    saved_errno = errno;
    getCurrentTime();
    debugs(5, 5, "do_comm_select: %d fds ready", num);
    if (num < 0) {
	if (ignoreErrno(saved_errno))
	    return COMM_OK;

	debugs(5, 1, "comm_select: devpoll ioctl(DP_POLL) failure: %s", xstrerror());
	return COMM_ERROR;
    }
    statHistCount(&statCounter.select_fds_hist, num);
    if (num == 0)
	return COMM_TIMEOUT;

    for (i = 0; i < num; i++) {
	int fd = (int) do_poll.dp_fds[i].fd;
	if (do_poll.dp_fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
	    debugs(5, 1, "comm_select: devpoll event error: fd %d", fd);
	    continue;		/* XXX! */
	}
	if (do_poll.dp_fds[i].revents & POLLIN) {
	    comm_call_handlers(fd, 1, 0);
	}
	if (do_poll.dp_fds[i].revents & POLLOUT) {
	    comm_call_handlers(fd, 0, 1);
	}
    }

    return COMM_OK;
}
