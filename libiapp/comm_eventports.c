
/*
 * $Id: comm_kqueue.c 11903 2007-05-20 13:45:11Z adrian $
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

#include "squid.h"
#include "comm_generic.c"

#include <port.h>

#define EV_LIST_SIZE	128

static int ev_fd;
static port_event_t *evlist;

static void
do_select_init()
{
    ev_fd = port_create();
    if (ev_fd < 0)
	fatalf("comm_select_init: port_create(): %s\n", xstrerror());
    fd_open(ev_fd, FD_UNKNOWN, "evport ctl");
    commSetCloseOnExec(ev_fd);
    evlist = xcalloc(EV_LIST_SIZE, sizeof(port_event_t));
}

void
comm_select_postinit()
{
    debugs(5, 1, "Using Solaris Event Ports for the IO loop");
}

static void
do_select_shutdown()
{
    fd_close(ev_fd);
    close(ev_fd);
    ev_fd = -1;
    safe_free(evlist);
}

const char *
comm_select_status(void)
{
    return("event ports");
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
    int st_new = (need_read ? POLLIN : 0) | (need_write ? POLLOUT : 0);

    assert(fd >= 0);
    debugs(5, 8, "commSetEvents(fd=%d, read=%d, write=%d)", fd, need_read, need_write);

    if (st_new == 0)
        port_dissociate(ev_fd, PORT_SOURCE_FD, fd);
    else
        port_associate(ev_fd, PORT_SOURCE_FD, fd, st_new, NULL);
}

static int
do_comm_select(int msec)
{
    int i;
    struct timespec timeout;
    uint_t num = 1;
    int r;

    timeout.tv_sec = msec / 1000;
    timeout.tv_nsec = (msec % 1000) * 1000000;

    statCounter.syscalls.polls++;
    r = port_getn(ev_fd, evlist, (uint_t) EV_LIST_SIZE, (uint_t *) &num, &timeout);

    if (r < 0) {
	getCurrentTime();
        if (errno == ETIME)
            return COMM_TIMEOUT;
	if (ignoreErrno(errno))
	    return COMM_OK;

	debugs(5, 1, "comm_select: port_getn() failure: %s", xstrerror());
	return COMM_ERROR;
    }
    statHistCount(&statCounter.select_fds_hist, num);
    if (num == 0)
	return COMM_TIMEOUT;

    for (i = 0; i < num; i++) {
        assert(evlist[i].portev_source == PORT_SOURCE_FD);
	int fd = (int) evlist[i].portev_object;
        if (evlist[i].portev_events & POLLIN) 
	    comm_call_handlers(fd, 1, 0);
	if (evlist[i].portev_events & POLLOUT)
	    comm_call_handlers(fd, 0, 1);
    }

    return COMM_OK;
}
