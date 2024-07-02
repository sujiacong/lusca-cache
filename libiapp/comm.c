
/*
 * $Id: comm.c 12809 2008-06-05 03:16:05Z adrian.chadd $
 *
 * DEBUG: section 5     Socket Functions
 * AUTHOR: Harvest Derived
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

/* On native Windows, squid_mswin.h needs to know when we are compiling
 * comm.c for the correct handling of FD<=>socket magic
 */
#define COMM_C

#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
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
#if HAVE_NETDB_H
#include <netdb.h>
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
 
#include "../libcb/cbdata.h"

#include "../libsqinet/sqinet.h"

#include "../libstat/StatHist.h"
 
#ifdef _SQUID_MSWIN_
#include "win32_pipe.h"
#endif
#include "iapp_ssl.h"
#include "globals.h"
#include "fd_types.h"
#include "comm_types.h"
#include "comm.h"
#include "pconn_hist.h"
#include "comm_ips.h"

#include "ssl_support.h"

#if defined(_SQUID_CYGWIN_)
#include <sys/ioctl.h>
#endif
#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif

/* STATIC */
static void commSetReuseAddr(int);
static void commSetNoLinger(int);
static void CommWriteStateCallbackAndFree(int fd, int code);
static PF commHandleWrite;

static MemPool *comm_write_pool = NULL;
static MemPool *conn_close_pool = NULL;

struct in_addr local_addr;
struct in_addr no_addr;

extern time_t squid_curtime;	/* XXX */

CommStatStruct CommStats;

static void
CommWriteStateCallbackAndFree(int fd, int code)
{
    CommWriteStateData *CommWriteState = &fd_table[fd].rwstate;
    CWCB *callback = NULL;
    void *data;
    if (!CommWriteState->valid) {
	return;
    }
    CommWriteState->valid = 0;
    if (CommWriteState->free_func) {
	FREE *free_func = CommWriteState->free_func;
	void *free_buf = CommWriteState->buf;
	CommWriteState->free_func = NULL;
	CommWriteState->buf = NULL;
	free_func(free_buf);
    }
    callback = CommWriteState->handler;
    data = CommWriteState->handler_data;
    CommWriteState->handler = NULL;
    CommWriteState->valid = 0;
    if (callback && cbdataValid(data))
	callback(fd, CommWriteState->buf, CommWriteState->offset, code, data);
    cbdataUnlock(data);
}

/* Return the local port associated with fd. */
u_short
comm_local_port(int fd)
{
    sqaddr_t addr;
    socklen_t addr_len = 0;
    fde *F = &fd_table[fd];

    /* If the fd is closed already, just return */
    if (!F->flags.open) {
	debug(5, 0) ("comm_local_port: FD %d has been closed.\n", fd);
	return 0;
    }
    if (F->local_port)
	return F->local_port;

    sqinet_init(&addr);
    addr_len = sqinet_get_length(&addr);
    if (getsockname(fd, sqinet_get_entry(&addr), &addr_len)) {
	debug(5, 1) ("comm_local_port: Failed to retrieve TCP/UDP port number for socket: FD %d: %s\n", fd, xstrerror());
	return 0;
    }
    F->local_port = sqinet_get_port(&addr);
    debug(5, 6) ("comm_local_port: FD %d: port %d\n", fd, (int) F->local_port);
    sqinet_done(&addr);
    return F->local_port;
}

int
commBind(int s, sqaddr_t *addr)
{
    int r;

    LOCAL_ARRAY(char, ip_buf, MAX_IPSTRLEN);
    CommStats.syscalls.sock.binds++;
    if (bind(s, sqinet_get_entry(addr), sqinet_get_length(addr)) == 0)
	return COMM_OK;
    r = sqinet_ntoa(addr, ip_buf, MAX_IPSTRLEN, 0);
    if (r)
        debug(5, 0) ("commBind: Cannot bind socket FD %d family %d to %s port %d: %s\n",
          s, sqinet_get_family(addr), ip_buf, sqinet_get_port(addr), xstrerror());
    else
        debug(5, 0) ("commBind: Cannot bind socket FD %d family %d: %s\n",
          s, sqinet_get_family(addr), xstrerror());
    return COMM_ERROR;
}

int
comm_open(int sock_type, int proto, struct in_addr addr, u_short port, comm_flags_t flags, unsigned char TOS, const char *note)
{
	sqaddr_t a;
	int r;

	sqinet_init(&a);
	sqinet_set_v4_inaddr(&a, &addr);
	sqinet_set_v4_port(&a, port, SQADDR_ASSERT_IS_V4);
	r = comm_open6(sock_type, proto, &a, flags, TOS, note);
	sqinet_done(&a);
	return r;
}


/* Create a socket. Default is blocking, stream (TCP) socket.  IO_TYPE
 * is OR of flags specified in defines.h:COMM_* */
int
comm_open6(int sock_type,
    int proto,
    sqaddr_t *a,
    comm_flags_t flags,
    unsigned char TOS,
    const char *note)
{
    int new_socket;
    int tos = 0;

    /* Create socket for accepting new connections. */
    CommStats.syscalls.sock.sockets++;
    if ((new_socket = socket(sqinet_get_family(a), sock_type, proto)) < 0) {
	/* Increase the number of reserved fd's if calls to socket()
	 * are failing because the open file table is full.  This
	 * limits the number of simultaneous clients */
	switch (errno) {
	case ENFILE:
	case EMFILE:
	    debug(5, 1) ("comm_open: socket failure: %s\n", xstrerror());
	    fdAdjustReserved();
	    break;
	default:
	    debug(5, 0) ("comm_open: socket failure: %s\n", xstrerror());
	}
	return -1;
    }
    /* set TOS if needed */
    if (TOS) {
#ifdef IP_TOS
	tos = TOS;
	if (setsockopt(new_socket, IPPROTO_IP, IP_TOS, (char *) &tos, sizeof(int)) < 0)
	        debug(5, 1) ("comm_open: setsockopt(IP_TOS) on FD %d: %s\n",
		new_socket, xstrerror());
#else
	debug(5, 0) ("comm_open: setsockopt(IP_TOS) not supported on this platform\n");
#endif
    }
    /* update fdstat */
    debug(5, 5) ("comm_openex: FD %d is a new socket\n", new_socket);
    return comm_fdopen6(new_socket, sock_type, a, flags, tos, note);
}

int
comm_fdopen(int new_socket, int sock_type, struct in_addr addr, u_short port, comm_flags_t flags, unsigned char tos, const char *note)
{
	sqaddr_t a;
	int r;

	sqinet_init(&a);
	sqinet_set_v4_inaddr(&a, &addr);
	sqinet_set_v4_port(&a, port, SQADDR_ASSERT_IS_V4);
	r = comm_fdopen6(new_socket, sock_type, &a, flags, tos, note);
	sqinet_done(&a);
	return r;
}

int
comm_fdopen6(int new_socket,
    int sock_type,
    sqaddr_t *a,
    comm_flags_t flags,
    unsigned char tos,
    const char *note)
{
    fde *F = NULL;

    fd_open(new_socket, FD_SOCKET, note);
    F = &fd_table[new_socket];

    sqinet_init(&(F->local_address));
    sqinet_init(&(F->remote_address));
    sqinet_copy(&(F->local_address), a);

    F->tos = tos;
    if (!(flags & COMM_NOCLOEXEC))
	commSetCloseOnExec(new_socket);
    if ((flags & COMM_REUSEADDR))
	commSetReuseAddr(new_socket);
    if ((flags & COMM_TPROXY_LCL))
      F->flags.tproxy_lcl = 1;
    if ((flags & COMM_TPROXY_REM))
      F->flags.tproxy_rem = 1;
    if (sqinet_get_port(a) > 0) {
#ifdef _SQUID_MSWIN_
	if (sock_type != SOCK_DGRAM)
#endif
	    commSetNoLinger(new_socket);
	if (opt_reuseaddr)
	    commSetReuseAddr(new_socket);
    }

    /*
     * The local endpoint bind() stuff is a bit of a mess.
     *
     * There's three kinds of "bind" going on.
     *    
     * The default bind(), for a normal socket. This is used both for setting the listen address for
     * an incoming socket and the local address of a remote connection socket.
     *
     * The "spoof connection" bind, which is a bind() to a non-local address, for a remote connection
     * socket. Ie, spoofing the client IP when connecting to an upstream.
     *
     * Finally, the "non-local listen" bind, which is a bind() for the purposes of intercepting
     * connections which aren't targetted at a local IP.
     *
     * These are all treated differently by the various interception techniques on various operating
     * systems; this is why things are broken out.
     */
    if (F->flags.tproxy_lcl) {
        if (comm_ips_bind_lcl(new_socket, &F->local_address) != COMM_OK) {
            debug(1, 1) ("comm_fdopen6: FD %d: TPROXY comm_ips_bind_lcl() failed? Why?\n", new_socket);
            comm_close(new_socket);
            return -1;
        }
    } else if (F->flags.tproxy_rem) {
        if (comm_ips_bind_rem(new_socket, &F->local_address) != COMM_OK) {
            debug(1, 1) ("comm_fdopen6: FD %d: TPROXY comm_ips_bind_rem() failed: errno %d (%s)\n", new_socket, errno, xstrerror());
            comm_close(new_socket);
            return -1;
        }
    } else if (! sqinet_is_noaddr(&F->local_address)) {
	if (commBind(new_socket, &F->local_address) != COMM_OK) {
	    comm_close(new_socket);
	    return -1;
	}
    }
    F->local_port = sqinet_get_port(a);

    if (flags & COMM_NONBLOCKING)
	if (commSetNonBlocking(new_socket) == COMM_ERROR)
	    return -1;
    if (sock_type == SOCK_STREAM)
	commSetTcpNoDelay(new_socket);
    if (iapp_tcpRcvBufSz > 0 && sock_type == SOCK_STREAM)
	commSetTcpRcvbuf(new_socket, iapp_tcpRcvBufSz);
    return new_socket;
}

/*
 * NOTE: set the listen queue to Squid_MaxFD/4 and rely on the kernel to      
 * impose an upper limit.  Solaris' listen(3n) page says it has   
 * no limit on this parameter, but sys/socket.h sets SOMAXCONN 
 * to 5.  HP-UX currently has a limit of 20.  SunOS is 5 and
 * OSF 3.0 is 8.
 */
int
comm_listen(int sock)
{
    int x;
    if ((x = listen(sock, Squid_MaxFD >> 2)) < 0) {
	debug(5, 0) ("comm_listen: listen(%d, %d): %s\n",
	    Squid_MaxFD >> 2,
	    sock, xstrerror());
	return x;
    }
    if (iapp_useAcceptFilter && strcmp(iapp_useAcceptFilter, "none") != 0) {
#ifdef SO_ACCEPTFILTER
	struct accept_filter_arg afa;
	memset(&afa, 0, sizeof(afa));
	debug(5, 0) ("Installing accept filter '%s' on FD %d\n",
	    iapp_useAcceptFilter, sock);
	xstrncpy(afa.af_name, iapp_useAcceptFilter, sizeof(afa.af_name));
	x = setsockopt(sock, SOL_SOCKET, SO_ACCEPTFILTER, &afa, sizeof(afa));
	if (x < 0)
	    debug(5, 0) ("SO_ACCEPTFILTER '%s': %s\n", iapp_useAcceptFilter, xstrerror());
#elif defined(TCP_DEFER_ACCEPT)
	int seconds = 30;
	if (strncmp(iapp_useAcceptFilter , "data=", 5) == 0)
	    seconds = atoi(iapp_useAcceptFilter + 5);
	x = setsockopt(sock, IPPROTO_TCP, TCP_DEFER_ACCEPT, &seconds, sizeof(seconds));
	if (x < 0)
	    debug(5, 0) ("TCP_DEFER_ACCEPT '%s': %s\n", iapp_useAcceptFilter, xstrerror());
#else
	debug(5, 0) ("accept_filter not supported on your OS\n");
#endif
    }
    return sock;
}

int
commSetTimeout(int fd, int timeout, PF * handler, void *data)
{
    fde *F;
    debug(5, 3) ("commSetTimeout: FD %d timeout %d\n", fd, timeout);
    assert(fd >= 0);
    assert(fd < Squid_MaxFD);
    F = &fd_table[fd];
    assert(F->flags.open);
    if (timeout < 0) {
	F->timeout_handler = NULL;
	F->timeout_data = NULL;
	return F->timeout = 0;
    }
    assert(handler || F->timeout_handler);
    if (handler || data) {
	F->timeout_handler = handler;
	F->timeout_data = data;
    }
    return F->timeout = squid_curtime + (time_t) timeout;
}

/*!
 * @function
 *	comm_connect_try
 * @abstract
 *	Attempt an async callback-driven socket connect()
 * @discussion
 *	This is the IO callback for comm_connect_begin(); it handles the actual
 *	connection attempt and will either reschedule the callback or call the
 *	completion callback with the return state from conn_connect_addr().
 *
 * @param	fd	filedescriptor to try and connect()
 * @param	data	unused
 */
static void
comm_connect_try(int fd, void *data)
{
	int r;
	fde *F;
	CNCB *cb;
	void *cbdata;

	assert(fd >= 0);
	assert(fd < Squid_MaxFD);
	F = &fd_table[fd];
	assert(F->flags.open);
	assert(F->comm.connect.active);

	debug(5, 3) ("comm_connect_try: FD %d\n", fd);
	r = comm_connect_addr(fd, &F->comm.connect.addr);
	debug(5, 3) ("comm_connect_try: FD %d: retval %d\n", fd, r);
	if (r == COMM_INPROGRESS) {
		debug(5, 3) ("comm_connect_try: FD %d: retrying!\n", fd);
		commSetSelect(fd, COMM_SELECT_WRITE, comm_connect_try, NULL, 0);
		return;
	}

	/* completion has occured either way - call the callback with the connect results */
	debug(5, 3) ("comm_connect_try: FD %d: completed (%s)!\n", fd, r == COMM_OK ? "OK" : "FAIL");
	cb = F->comm.connect.cb;
	cbdata = F->comm.connect.cbdata;
	F->comm.connect.cb = NULL;
	F->comm.connect.cbdata = NULL;
	F->comm.connect.active = 0;
	sqinet_done(&F->comm.connect.addr);
	if (cbdataValid(cbdata))
		cb(fd, r, cbdata);
	cbdataUnlock(cbdata);
}

/*!
 * @function
 *	comm_connect_begin
 * @abstract
 *	Begin an asynchronous callback-driven connect() process.
 * @discussion
 *	This function mirrors existing functionality in commConnectStart() but without
 *	the seperate memory allocation per attempt and the DNS lookup. commConnectStart()
 *	handles DNS resolution if required through the fqdncache; noting up/down
 *	hosts and handling IP address rotation for hosts as required.
 *
 *	Essentially, this is a lower-level function intended to replace most of
 *	the logic of commConnectStart() and related calls.
 *
 *	This function has no way of cancelling pending connect()s; it'll just not call the
 *	callback on an invalid cbdata pointer.
 *
 *	PREVIOUS COMMENT:
 *	The callback will currently never be called if comm_close() is called before
 *	the connect() has had time to complete (successfully or not.) This is in line with
 *	other comm_ related callbacks but may not be such a good idea moving forward.
 *
 *	CURRENT COMMENT:
 *	comm_close() is calling the connect completion callback if it hasn't yet finished..?
 *	That needs to be sorted out!
 *
 *	Like commConnectStart(), the connect() -may- succeed after the first call and
 *	the callback -may- be immediately called. Too much existing code (well, all 8 uses)
 *	currently relies on and works with this behaviour. This should be investigated
 *	at a later date so callbacks occur -seperate- to their IO events having completed.
 *	This will however require some code auditing!
 *
 * @param	fd	currently open filedescriptor to connect() to remote host
 * @param	addr	sqaddr_t reference containing end-point information
 * @param	cb	callback to call on completion
 * @param	cbdata	callback data for above callback (NULL permitted)
 */
void
comm_connect_begin(int fd, const sqaddr_t *addr, CNCB *cb, void *cbdata)
{
	fde *F;
	assert(fd >= 0);
	assert(fd < Squid_MaxFD);
	F = &fd_table[fd];
	assert(F->flags.open);

	debug(5, 3) ("comm_connect_begin: FD %d\n", fd);

	/* XXX must never, ever call comm_connect_begin() on a connecting socket! */
	assert(! F->comm.connect.active);

	/* Record info */
	F->comm.connect.cb = cb;
	F->comm.connect.cbdata = cbdata;
	cbdataLock(F->comm.connect.cbdata);
	sqinet_init(&F->comm.connect.addr);
	sqinet_copy(&F->comm.connect.addr, addr);
	F->comm.connect.active = 1;

	/* Begin attempting to connect */
	comm_connect_try(fd, NULL);
}

int
comm_connect_addr(int sock, const sqaddr_t *addr)
{
    int status = COMM_OK;
    fde *F = &fd_table[sock];
    int x;
    int err = 0;
    socklen_t errlen;
    assert(sqinet_get_port(addr) != 0);
    /* Establish connection. */
    errno = 0;
    if (!F->flags.called_connect) {
	F->flags.called_connect = 1;
	CommStats.syscalls.sock.connects++;
	x = connect(sock, sqinet_get_entry_ro(addr), sqinet_get_length(addr));
	if (x < 0)
	    debug(5, 9) ("connect FD %d: %s\n", sock, xstrerror());
    } else {
#if defined(_SQUID_NEWSOS6_)
	/* Makoto MATSUSHITA <matusita@ics.es.osaka-u.ac.jp> */
	connect(sock, sqinet_get_entry(addr), sqinet_get_length(addr));
	if (errno == EINVAL) {
	    errlen = sizeof(err);
	    x = getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &errlen);
	    if (x >= 0)
		errno = x;
	}
#else
	errlen = sizeof(err);
	x = getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &errlen);
	if (x == 0)
	    errno = err;
#if defined(_SQUID_SOLARIS_)
	/*
	 * Solaris 2.4's socket emulation doesn't allow you
	 * to determine the error from a failed non-blocking
	 * connect and just returns EPIPE.  Create a fake
	 * error message for connect.   -- fenner@parc.xerox.com
	 */
	if (x < 0 && errno == EPIPE)
	    errno = ENOTCONN;
#endif
#endif
    }
    if (errno == 0 || errno == EISCONN)
	status = COMM_OK;
    else if (ignoreErrno(errno))
	status = COMM_INPROGRESS;
    else
	return COMM_ERROR;
    sqinet_ntoa(addr, F->ipaddrstr, MAX_IPSTRLEN, 0);
    sqinet_copy(&F->remote_address, addr);
    F->remote_port = sqinet_get_port(addr);
    if (status == COMM_OK) {
	debug(5, 10) ("comm_connect_addr: FD %d connected to %s:%d\n",
	    sock, F->ipaddrstr, F->remote_port);
    } else if (status == COMM_INPROGRESS) {
	debug(5, 10) ("comm_connect_addr: FD %d connection pending\n", sock);
    }
    return status;
}

/* Wait for an incoming connection on FD.  FD should be a socket returned
 * from comm_listen. */
int
comm_accept(int fd, sqaddr_t *pn, sqaddr_t *me)
{
    int sock;
    int ret = COMM_OK;
    sqaddr_t loc, rem;

    socklen_t Slen;
    fde *F = NULL;

    sqinet_init(&loc);
    sqinet_init(&rem);
    Slen = sqinet_get_maxlength(&rem);

    CommStats.syscalls.sock.accepts++;
    if ((sock = accept(fd, sqinet_get_entry(&rem), &Slen)) < 0) {
	if (ignoreErrno(errno) || errno == ECONNREFUSED || errno == ECONNABORTED) {
	    debug(5, 5) ("comm_accept: FD %d: %s\n", fd, xstrerror());
            ret = COMM_NOMESSAGE;
	    goto finish;
	} else if (ENFILE == errno || EMFILE == errno) {
	    debug(5, 3) ("comm_accept: FD %d: %s\n", fd, xstrerror());
            ret = COMM_ERROR;
	    goto finish;
	} else {
	    debug(5, 1) ("comm_accept: FD %d: %s\n", fd, xstrerror());
            ret = COMM_ERROR;
	    goto finish;
	}
    }
    if (pn)
	sqinet_copy(pn, &rem);
    Slen = sqinet_get_maxlength(&loc);
    getsockname(sock, sqinet_get_entry(&loc), &Slen);
    if (me)
        sqinet_copy(me, &loc);
    commSetCloseOnExec(sock);
    /* fdstat update */
    fd_open(sock, FD_SOCKET, NULL);
    fd_note_static(sock, "HTTP Request");
    F = &fd_table[sock];
    sqinet_ntoa(&rem, F->ipaddrstr, MAX_IPSTRLEN, 0);
    sqinet_copy(&F->remote_address, &rem);
    F->remote_port = sqinet_get_port(&rem);
    F->local_port = sqinet_get_port(&loc);
    commSetNonBlocking(sock);
    ret = sock;
finish:
    sqinet_done(&loc);
    sqinet_done(&rem);
    return ret;
}

void
commCallCloseHandlers(int fd)
{
    fde *F = &fd_table[fd];
    close_handler *ch;
    debug(5, 5) ("commCallCloseHandlers: FD %d\n", fd);
    while ((ch = F->close_handler) != NULL) {
	F->close_handler = ch->next;
	debug(5, 5) ("commCallCloseHandlers: ch->handler=%p\n", ch->handler);
	if (cbdataValid(ch->data))
	    ch->handler(fd, ch->data);
	cbdataUnlock(ch->data);
	memPoolFree(conn_close_pool, ch);	/* AAA */
    }
}

#if LINGERING_CLOSE
static void
commLingerClose(int fd, void *unused)
{
    LOCAL_ARRAY(char, buf, 1024);
    int n;
    n = FD_READ_METHOD(fd, buf, 1024);
    if (n < 0)
	debug(5, 3) ("commLingerClose: FD %d read: %s\n", fd, xstrerror());
    comm_close(fd);
}

#if USE_SSL
static void
commLingerSSLClose(int fd, void *unused)
{
    int ret;
    LOCAL_ARRAY(char, buf, 1024);

    ret = FD_READ_METHOD(fd, buf, 1024);
    if (n < 0 && errno != EAGAIN) {
	debug(5, 3) ("commLingerSSLClose: FD %d read: %s\n", fd, xstrerror());
	comm_close(fd);
	return;
    }
    ret = ssl_shutdown_method(fd);
    if (ret == -1 && errno == EAGAIN) {
	commSetSelect(fd, COMM_SELECT_WRITE, commLingerSSLClose, NULL, 0);
	return;
    }
    if (shutdown(fd, 1) < 0) {
	comm_close(fd);
	return;
    }
    commSetSelect(fd, COMM_SELECT_READ, commLingerClose, NULL, 0);
}
#endif

static void
commLingerTimeout(int fd, void *unused)
{
    debug(5, 3) ("commLingerTimeout: FD %d\n", fd);
    comm_close(fd);
}

/*
 * Inspired by apache
 */
void
comm_lingering_close(int fd)
{
    fd_note_static(fd, "lingering close");
    commSetSelect(fd, COMM_SELECT_READ, NULL, NULL, 0);
    commSetSelect(fd, COMM_SELECT_WRITE, NULL, NULL, 0);
    commSetTimeout(fd, 10, commLingerTimeout, NULL);
#if USE_SSL
    if (fd_table[fd].ssl) {
	commLingerSSLClose(fd, NULL);
	return;
    }
#endif
    if (shutdown(fd, 1) < 0) {
	comm_close(fd);
	return;
    }
    commSetSelect(fd, COMM_SELECT_READ, commLingerClose, NULL, 0);
}
#endif

/*
 * enable linger with time of 0 so that when the socket is
 * closed, TCP generates a RESET
 */
void
comm_reset_close(int fd)
{
    fde *F = &fd_table[fd];
    struct linger L;
    L.l_onoff = 1;
    L.l_linger = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_LINGER, (char *) &L, sizeof(L)) < 0)
	debug(5, 0) ("comm_reset_close: FD %d: %s\n", fd, xstrerror());
    F->flags.close_request = 1;
    comm_close(fd);
}

static inline void
comm_close_finish(int fd)
{
    sqinet_done(&fd_table[fd].local_address);
    sqinet_done(&fd_table[fd].remote_address);
    fd_close(fd);		/* update fdstat */
    close(fd);
    CommStats.syscalls.sock.closes++;
}

#if USE_SSL
static inline void
comm_close_ssl_finish(int fd)
{
    fde *F = &fd_table[fd];
    SSL_free(F->ssl);
    F->ssl = NULL;
    comm_close_finish(fd);
}

static void
comm_close_ssl(int fd, void *unused)
{
    fde *F = &fd_table[fd];
    int ret = ssl_shutdown_method(fd);
    if (ret <= 0 && F->write_pending) {
	commSetSelect(fd, COMM_SELECT_WRITE, comm_close_ssl, NULL, 0);
	return;
    }
    comm_close_ssl_finish(fd);
}

static void
comm_close_ssl_timeout(int fd, void *unused)
{
    debug(5, 1) ("comm_close_ssl_timeout: FD %d: timeout\n", fd);
    comm_close_ssl_finish(fd);
}

#endif

/*!
 * @function
 *	comm_close
 * @abstract
 *	Begin the process of closing down a socket / comm filedescriptor.
 * @discussion
 *	A bunch of things happen:
 *	+ Quit if the FD is a file, or is closing, or Squid is shutting down
 *	+ If the FD was connecting to a remote host, call the connect
 *	  completion callback with COMM_ERR_CLOSING
 *	+ call the close handlers
 *	+ Update the pconn counters
 *	+ If SSL; mark as closing down, and begin closing.. ?
 *	+ Finish the closing down process (ie, fd_close(), etc.)
 *
 *	TODO:
 *	    Unlike file_close(), I'm not sure whether this calls the
 *	    completion callback(s) anywhere on closing. This should
 *	    be documented and implemented!
 *
 * @param	fd		File descriptor to close
 */
void
comm_close(int fd)
{
    fde *F = &fd_table[fd];

    debug(5, 5) ("comm_close: FD %d\n", fd);
    assert(fd >= 0);
    assert(fd < Squid_MaxFD);

    /* XXX This down to the cavium block below needs to be split and
     * also called once on lingering close. In addition the ssl_shutdown
     * may need to wait
     */
    if (F->flags.closing)
	return;
    if (shutting_down && (!F->flags.open || F->type == FD_FILE))
	return;
    assert(F->flags.open);
    assert(F->type != FD_FILE);
    F->flags.closing = 1;
    if (F->comm.connect.active)
	cbdataUnlock(F->comm.connect.cbdata);
    CommWriteStateCallbackAndFree(fd, COMM_ERR_CLOSING);
    commCallCloseHandlers(fd);
    if (F->uses)		/* assume persistent connect count */
	pconnHistCount(1, F->uses);
#if USE_SSL
    if (F->ssl) {
	if (!F->flags.close_request) {
	    F->flags.close_request = 1;
	    commSetTimeout(fd, 10, comm_close_ssl_timeout, NULL);
	    comm_close_ssl(fd, NULL);
	    return;
	}
	comm_close_ssl_finish(fd);
	return;
    }
#endif
    comm_close_finish(fd);
}

/* Send a udp datagram to specified TO_ADDR. */
int
comm_udp_sendto6(int fd,
    const sqaddr_t *to_addr,
    const void *buf,
    int len)
{
    int x;
    LOCAL_ARRAY(char, sbuf, 256);
    CommStats.syscalls.sock.sendtos++;
    x = sendto(fd, buf, len, 0, sqinet_get_entry_ro(to_addr), sqinet_get_length(to_addr));
    if (x < 0) {
        (void) sqinet_ntoa(to_addr, sbuf, sizeof(sbuf), SQADDR_NONE);
#ifdef _SQUID_LINUX_
	if (ECONNREFUSED != errno)
#endif
	    debug(5, 1) ("comm_udp_sendto: FD %d, %s, port %d: %s\n",
		fd,
		sbuf,
		(int) sqinet_get_port(to_addr),
		xstrerror());
	return COMM_ERROR;
    }
    return x;
}

int
comm_udp_sendto(int fd, const struct sockaddr_in *to_addr, int addr_len, const void *buf, int len)
{
	sqaddr_t A;
	int i;

	sqinet_init(&A);
	sqinet_set_v4_sockaddr(&A, to_addr);
	i = comm_udp_sendto6(fd, &A, buf, len);
	sqinet_done(&A);
	return i;
}
void
commSetDefer(int fd, DEFER * func, void *data)
{
    fde *F = &fd_table[fd];
    F->defer_check = func;
    F->defer_data = data;
}

void
commUpdateEvents(int fd)
{
    fde *F = &fd_table[fd];
    int need_read = 0;
    int need_write = 0;

    assert(F->flags.open);

    if (F->read_handler
	&& !F->flags.backoff
	) {
	switch (F->read_pending) {
	case COMM_PENDING_NORMAL:
	    need_read = 1;
	    break;
	case COMM_PENDING_WANTS_WRITE:
	    need_write = 1;
	    break;
	case COMM_PENDING_WANTS_READ:
	    need_read = 1;
	    break;
	case COMM_PENDING_NOW:
	    need_read = 1;	/* Not really I/O dependent, but this shuld get comm_select to wake up */
	    need_write = 1;
	    break;
	}
    }
    if (F->write_handler) {
	switch (F->write_pending) {
	case COMM_PENDING_NORMAL:
	    need_write = 1;
	    break;
	case COMM_PENDING_WANTS_WRITE:
	    need_write = 1;
	    break;
	case COMM_PENDING_WANTS_READ:
	    need_read = 1;
	    break;
	case COMM_PENDING_NOW:
	    need_read = 1;	/* Not really I/O dependent, but this shuld get comm_select to wake up */
	    need_write = 1;
	    break;
	}
    }
    commSetEvents(fd, need_read, need_write);
}

void
commUpdateReadHandler(int fd, PF * handler, void *data)
{
    fd_table[fd].read_handler = handler;
    fd_table[fd].read_data = data;
    if (!handler)
	fd_table[fd].read_pending = COMM_PENDING_NORMAL;
    commUpdateEvents(fd);
}

void
commUpdateWriteHandler(int fd, PF * handler, void *data)
{
    fd_table[fd].write_handler = handler;
    fd_table[fd].write_data = data;
    if (!handler)
	fd_table[fd].write_pending = COMM_PENDING_NORMAL;
    commUpdateEvents(fd);
}


/*!
 * @function
 *	commSetSelect
 * @abstract
 *	Register the given file descriptor for a comm notification
 * @discussion
 *	The completion callback will not be called on comm_close();
 *	so the owner needs to register a close handler if it wants
 *	to be told.
 *
 * @param	fd		comm file descriptor (disk.c does this too?)
 * @param	type		A union of COMM_SELECT_READ, COMM_SELECT_WRITE
 * @param	handler		Notification callback
 * @param	client_data	Notification callback data
 * @param	timeout		If > 0, set the FD timeout - which triggers the timeout handler
 */
void
commSetSelect(int fd, unsigned int type, PF * handler, void *client_data, time_t timeout)
{
    fde *F = &fd_table[fd];
    assert(fd >= 0);
    assert(F->flags.open);
    debug(5, 5) ("commSetSelect: FD %d type %d\n", fd, type);
    if (type & COMM_SELECT_READ) {
	commUpdateReadHandler(fd, handler, client_data);
    }
    if (type & COMM_SELECT_WRITE) {
	commUpdateWriteHandler(fd, handler, client_data);
    }
    if (timeout)
	F->timeout = squid_curtime + timeout;
}

void
comm_add_close_handler(int fd, PF * handler, void *data)
{
    close_handler *new = memPoolAlloc(conn_close_pool);		/* AAA */
    close_handler *c;
    debug(5, 5) ("comm_add_close_handler: FD %d, handler=%p, data=%p\n",
	fd, handler, data);
    for (c = fd_table[fd].close_handler; c; c = c->next)
	assert(c->handler != handler || c->data != data);
    new->handler = handler;
    new->data = data;
    new->next = fd_table[fd].close_handler;
    fd_table[fd].close_handler = new;
    cbdataLock(data);
}

void
comm_remove_close_handler(int fd, PF * handler, void *data)
{
    close_handler *p;
    close_handler *last = NULL;
    /* Find handler in list */
    debug(5, 5) ("comm_remove_close_handler: FD %d, handler=%p, data=%p\n",
	fd, handler, data);
    for (p = fd_table[fd].close_handler; p != NULL; last = p, p = p->next)
	if (p->handler == handler && p->data == data)
	    break;		/* This is our handler */
    assert(p != NULL);
    /* Remove list entry */
    if (last)
	last->next = p->next;
    else
	fd_table[fd].close_handler = p->next;
    cbdataUnlock(p->data);
    memPoolFree(conn_close_pool, p);	/* AAA */

}

static void
commSetNoLinger(int fd)
{
    struct linger L;
    L.l_onoff = 0;		/* off */
    L.l_linger = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_LINGER, (char *) &L, sizeof(L)) < 0)
	debug(5, 0) ("commSetNoLinger: FD %d: %s\n", fd, xstrerror());
    fd_table[fd].flags.nolinger = 1;
}

void
commSetNoPmtuDiscover(int fd)
{
#if defined(IP_MTU_DISCOVER) && defined(IP_PMTUDISC_DONT)
	int i = IP_PMTUDISC_DONT;
	(void) setsockopt(fd, SOL_IP, IP_MTU_DISCOVER, &i, sizeof i);
#else
	static int reported = 0;
	if (!reported) {
		debug(33, 1) ("Notice: httpd_accel_no_pmtu_disc not supported on your platform\n");
		reported = 1;
	}
#endif
}

static void
commSetReuseAddr(int fd)
{
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on)) < 0)
	debug(5, 1) ("commSetReuseAddr: FD %d: %s\n", fd, xstrerror());
}

void
commSetTcpRcvbuf(int fd, int size)
{
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *) &size, sizeof(size)) < 0)
	debug(5, 1) ("commSetTcpRcvbuf: FD %d, SIZE %d: %s\n",
	    fd, size, xstrerror());
}

int
commSetNonBlocking(int fd)
{
#ifdef _SQUID_MSWIN_
    unsigned long nonblocking = TRUE;

    if (ioctlsocket(fd, FIONBIO, &nonblocking) < 0) {
	debug(5, 0) ("commSetNonBlocking: FD %d: %s %u\n", fd, xstrerror(), fd_table[fd].type);
	return COMM_ERROR;
    }
#else /* _SQUID_MSWIN_ */

    int flags;
    int dummy = 0;

#ifdef _SQUID_CYGWIN_
    int nonblocking = TRUE;

    if (fd_table[fd].type != FD_PIPE) {
	if (ioctl(fd, FIONBIO, &nonblocking) < 0) {
	    debug(5, 0) ("commSetNonBlocking: FD %d: %s %u\n", fd, xstrerror(), fd_table[fd].type);
	    return COMM_ERROR;
	}
    } else {
#endif
	if ((flags = fcntl(fd, F_GETFL, dummy)) < 0) {
	    debug(5, 0) ("FD %d: fcntl F_GETFL: %s\n", fd, xstrerror());
	    return COMM_ERROR;
	}
	if (fcntl(fd, F_SETFL, flags | SQUID_NONBLOCK) < 0) {
	    debug(5, 0) ("commSetNonBlocking: FD %d: %s\n", fd, xstrerror());
	    return COMM_ERROR;
	}
#ifdef _SQUID_CYGWIN_
    }
#endif
#endif /* _SQUID_MSWIN_ */
    fd_table[fd].flags.nonblocking = 1;
    return 0;
}

int
commUnsetNonBlocking(int fd)
{
#ifdef _SQUID_MSWIN_
    unsigned long nonblocking = FALSE;
    if (ioctlsocket(fd, FIONBIO, &nonblocking) < 0) {
#else
    int flags;
    int dummy = 0;
    if ((flags = fcntl(fd, F_GETFL, dummy)) < 0) {
	debug(5, 0) ("FD %d: fcntl F_GETFL: %s\n", fd, xstrerror());
	return COMM_ERROR;
    }
    if (fcntl(fd, F_SETFL, flags & (~SQUID_NONBLOCK)) < 0) {
#endif
	debug(5, 0) ("commUnsetNonBlocking: FD %d: %s\n", fd, xstrerror());
	return COMM_ERROR;
    }
    fd_table[fd].flags.nonblocking = 0;
    return 0;
}

void
commSetCloseOnExec(int fd)
{
#ifdef FD_CLOEXEC
    int flags;
    int dummy = 0;
    if ((flags = fcntl(fd, F_GETFL, dummy)) < 0) {
	debug(5, 0) ("FD %d: fcntl F_GETFL: %s\n", fd, xstrerror());
	return;
    }
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
	debug(5, 0) ("FD %d: set close-on-exec failed: %s\n", fd, xstrerror());
    fd_table[fd].flags.close_on_exec = 1;
#endif
}

void
commSetTcpNoDelay(int fd)
{
    int on = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *) &on, sizeof(on)) < 0)
	debug(5, 1) ("commSetTcpNoDelay: FD %d: %s\n", fd, xstrerror());
    fd_table[fd].flags.nodelay = 1;
}

int
commSetTcpBufferSize(int fd, int size)
{
	int r, err = 1;
	int s = size;

	r = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *) &s, sizeof(s));
	if (r < 0)
		err = 0;

	s = size;
	r = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *) &s, sizeof(s));
	if (r < 0)
		err = 0;
	return err;
}

void
commSetTcpKeepalive(int fd, int idle, int interval, int timeout)
{
    int on = 1;
#ifdef TCP_KEEPCNT
    if (timeout && interval) {
	int count = (timeout + interval - 1) / interval;
	if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(on)) < 0)
	    debug(5, 1) ("commSetTcpKeepalive: FD %d: %s\n", fd, xstrerror());
    }
#endif
#ifdef TCP_KEEPIDLE
    if (idle) {
	if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(on)) < 0)
	    debug(5, 1) ("commSetTcpKeepalive: FD %d: %s\n", fd, xstrerror());
    }
#endif
#ifdef TCP_KEEPINTVL
    if (interval) {
	if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(on)) < 0)
	    debug(5, 1) ("commSetTcpKeepalive: FD %d: %s\n", fd, xstrerror());
    }
#endif
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *) &on, sizeof(on)) < 0)
	debug(5, 1) ("commSetTcpKeepalive: FD %d: %s\n", fd, xstrerror());
}

/*
 * Get the current TOS from the socket.
 *
 * This returns the current TOS as set on the socket. It does not return
 * the "tos" field from the comm struct.
 *
 * Note that at least Linux/FreeBSD only return IP_TOS values which have been
 * previously set on the socket. There is currently no supported method for
 * fetching the TOS bits set on an incoming packet stream.
 *
 * If the socket tos could not be read, -1 is returned.
 */
int
commGetSocketTos(int fd)
{
	int res;
	int tos;
	socklen_t len;

	len = sizeof(tos);

#ifdef IP_TOS
	res = getsockopt(fd, IPPROTO_IP, IP_TOS, &tos, &len);
#else
	errno = ENOSYS;
	tos = -1;
#endif
	return tos;
}

int
commSetTos(int fd, int tos)
{
    int res;
    fde *F = &fd_table[fd];
    if (F->tos == tos)
	return 0;
    F->tos = tos;
#ifdef IP_TOS
    res = setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
#else
    errno = ENOSYS;
    res = -1;
#endif
    if (res < 0)
	debug(33, 1) ("commSetTos: FD %d: %s\n", fd, xstrerror());
    return res;
}

int
commSetSocketPriority(int fd, int prio)
{
    int res;
#ifdef SO_PRIORITY
    res = setsockopt(fd, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(prio));
#else
    errno = ENOSYS;
    res = -1;
#endif
    if (res < 0)
	debug(33, 1) ("commSetSocketPriority: FD %d: %s\n", fd, xstrerror());
    return res;
}

int
commSetIPOption(int fd, uint8_t option, void *value, size_t size)
{
    int res;
#ifdef IP_OPTIONS
    char data[16];
    data[0] = option;
    data[1] = size;
    memcpy(&data[2], value, size);
    res = setsockopt(fd, IPPROTO_IP, IP_OPTIONS, data, size + 2);
#else
    errno = ENOSYS;
    res = -1;
#endif
    if (res < 0)
	debug(33, 1) ("commSetIPOption: FD %d: %s\n", fd, xstrerror());
    return res;
}

void
comm_init(void)
{
    fd_init();
    /* Keep a few file descriptors free so that we don't run out of FD's
     * after accepting a client but before it opens a socket or a file.
     * Since Squid_MaxFD can be as high as several thousand, don't waste them */
    RESERVED_FD = XMIN(100, Squid_MaxFD / 4);
    comm_write_pool = memPoolCreate("CommWriteStateData", sizeof(CommWriteStateData));
    conn_close_pool = memPoolCreate("close_handler", sizeof(close_handler));
    statHistIntInit(&select_fds_hist, 256);
}

/* Write to FD. */
static void
commHandleWrite(int fd, void *data)
{
    int len = 0;
    int nleft;
    CommWriteStateData *state = &fd_table[fd].rwstate;

    assert(state->valid);

    debug(5, 5) ("commHandleWrite: FD %d: off %ld, hd %ld, sz %ld.\n",
	fd, (long int) state->offset, (long int) state->header_size, (long int) state->size);

    nleft = state->size + state->header_size - state->offset;
    if (state->offset < state->header_size)
	len = FD_WRITE_METHOD(fd, state->header + state->offset, state->header_size - state->offset);
    else
	len = FD_WRITE_METHOD(fd, state->buf + state->offset - state->header_size, nleft);
    debug(5, 5) ("commHandleWrite: write() returns %d\n", len);
    fd_bytes(fd, len, FD_WRITE);
    CommStats.syscalls.sock.writes++;

    if (len == 0) {
	/* Note we even call write if nleft == 0 */
	/* We're done */
	if (nleft != 0)
	    debug(5, 1) ("commHandleWrite: FD %d: write failure: connection closed with %d bytes remaining.\n", fd, nleft);
	CommWriteStateCallbackAndFree(fd, nleft ? COMM_ERROR : COMM_OK);
    } else if (len < 0) {
	/* An error */
	if (fd_table[fd].flags.socket_eof) {
	    debug(5, 2) ("commHandleWrite: FD %d: write failure: %s.\n",
		fd, xstrerror());
	    CommWriteStateCallbackAndFree(fd, COMM_ERROR);
	} else if (ignoreErrno(errno)) {
	    debug(5, 10) ("commHandleWrite: FD %d: write failure: %s.\n",
		fd, xstrerror());
	    commSetSelect(fd,
		COMM_SELECT_WRITE,
		commHandleWrite,
		NULL,
		0);
	} else {
	    debug(5, 2) ("commHandleWrite: FD %d: write failure: %s.\n",
		fd, xstrerror());
	    CommWriteStateCallbackAndFree(fd, COMM_ERROR);
	}
    } else {
	/* A successful write, continue */
	state->offset += len;
	if (state->offset < state->size + state->header_size) {
	    /* Not done, reinstall the write handler and write some more */
	    commSetSelect(fd,
		COMM_SELECT_WRITE,
		commHandleWrite,
		NULL,
		0);
	} else {
	    CommWriteStateCallbackAndFree(fd, COMM_OK);
	}
    }
}

#if 0
/*
 * XXX WARNING: This isn't to be used yet - its still under testing!
 */
static void
commHandleRead(int fd, void *data)
{
	int ret;
	CRCB *cb = fd_table[fd].comm.read.cb;
	void *cbdata = fd_table[fd].comm.read.cbdata;

	debug(5, 5) ("commHandleRead: FD %d\n", fd);
	assert(fd_table[fd].flags.open);
	assert(fd_table[fd].comm.read.active);

	ret = FD_READ_METHOD(fd, fd_table[fd].comm.read.buf, fd_table[fd].comm.read.size);

	fd_table[fd].comm.read.active = 0;
	fd_table[fd].comm.read.buf = 0;
	fd_table[fd].comm.read.size = 0;
	fd_table[fd].comm.read.cb = NULL;
	fd_table[fd].comm.read.cbdata = NULL;

	if (ret < 0) {
		cb(fd, ret, COMM_ERROR, errno, cbdata);
	} else {
		/* XXX there's no explicit EOF! So EOF is COMM_OK + 0 size data */
		cb(fd, ret, COMM_OK, 0, cbdata);
	}
}

/*
 * Select a handler for read.
 *
 * The caller -must- ensure that the buffer / callback / callback data remain
 * valid until the completion of the call -or- successful cancellation of
 * the comm_read().
 *
 * Some APIs (eg POSIX AIO) do not guarantee operations can be cancelled -
 * they may complete before cancellation occurs. In these cases, the operation
 * may complete before or during the cancellation (and in particular, the buffer
 * will be read into!) so a failed cancellation must turn into "wait for the
 * now pending callback to fire before completing the shutdown process".
 *
 * XXX WARNING: This isn't to be used yet - its still under testing!
 */
void
comm_read(int fd, char *buf, int size, CRCB *cb, void *cbdata)
{
	debug(5, 5) ("comm_read: FD %d, buf %p, size %d, cb %p, data %p\n", fd, buf, size, cb, cbdata);
	assert(fd_table[fd].comm.read.active == 0);
	assert(fd_table[fd].flags.open);

	fd_table[fd].comm.read.buf = buf;
	fd_table[fd].comm.read.size = size;
	fd_table[fd].comm.read.cb = cb;
	fd_table[fd].comm.read.cbdata = cbdata;
	fd_table[fd].comm.read.active = 1;

	commSetSelect(fd, COMM_SELECT_READ, commHandleRead, NULL, 0);
}

/*
 * For now, just cancel it if its active; the operations aren't done asynchronous.
 * This will change!
 *
 * XXX WARNING: This isn't to be used yet - its still under testing!
 */
int
comm_read_cancel(int fd)
{
	debug(5, 5) ("comm_read_cancel: FD %d\n", fd);
	if (! fd_table[fd].comm.read.active)
		return -1;

	fd_table[fd].comm.read.active = 0;
	commSetSelect(fd, COMM_SELECT_READ, NULL, NULL, 0);
	return 1;
}
#endif

/*!
 * @function
 *	comm_write
 * @abstract
 *	Write the data at {buf, size} to the given file descriptor.
 *
 *	Call {handler, handler_data} on completion IFF handler_data is still valid.
 *
 *	Call free_func on buf on completion.
 *
 * @discussion
 *	The data -will- be written to the socket regardless of whether the
 *	handler_data cbdata pointer is valid or not. The caller MUST make
 *	sure the data buffer remains valid for the duration of the call.
 *
 *	The callback will also not happen if the socket is closed before the
 *	write has fully completed.
 */
void
comm_write(int fd, const char *buf, int size, CWCB * handler, void *handler_data, FREE * free_func)
{
    CommWriteStateData *state = &fd_table[fd].rwstate;
    debug(5, 5) ("comm_write: FD %d: sz %d: hndl %p: data %p.\n",
	fd, size, handler, handler_data);
    if (state->valid) {
	debug(5, 1) ("comm_write: fd_table[%d].rwstate.valid == true!\n", fd);
	fd_table[fd].rwstate.valid = 0;
    }
    state->buf = (char *) buf;
    state->size = size;
    state->header_size = 0;
    state->offset = 0;
    state->handler = handler;
    state->handler_data = handler_data;
    state->free_func = free_func;
    state->valid = 1;
    cbdataLock(handler_data);
    commSetSelect(fd, COMM_SELECT_WRITE, commHandleWrite, NULL, 0);
}

/*!
 * @function
 *	comm_write_header
 * @abstract
 *	Write the header at {header, header_size} and the data at {buf, size} to
 *	the given file descriptor.
 *
 *	Call {handler, handler_data} on completion IFF handler_data is still valid.
 *
 *	Call free_func on buf on completion.
 *
 * @discussion
 *	The data -will- be written to the socket regardless of whether the
 *	handler_data cbdata pointer is valid or not. The caller MUST make
 *	sure the data buffer remains valid for the duration of the call.
 *
 *	The callback will also not happen if the socket is closed before the
 *	write has fully completed.
 */
void
comm_write_header(int fd, const char *buf, int size, const char *header, size_t header_size, CWCB * handler, void *handler_data, FREE * free_func)
{
    CommWriteStateData *state = &fd_table[fd].rwstate;
    debug(5, 5) ("comm_write_header: FD %d: sz %d: hndl %p: data %p.\n",
	fd, size, handler, handler_data);
    if (state->valid) {
	debug(5, 1) ("comm_write_header: fd_table[%d].rwstate.valid == true!\n", fd);
	fd_table[fd].rwstate.valid = 0;
    }
    state->buf = (char *) buf;
    state->size = size;
    state->offset = 0;
    state->handler = handler;
    state->handler_data = handler_data;
    cbdataLock(handler_data);
    state->free_func = free_func;
    state->valid = 1;
    assert(header_size < sizeof(state->header));
    memcpy(state->header, header, header_size);
    state->header_size = header_size;
    commSetSelect(fd, COMM_SELECT_WRITE, commHandleWrite, NULL, 0);
}

/* a wrapper around comm_write to allow for MemBuf to be comm_written in a snap */
void
comm_write_mbuf(int fd, MemBuf mb, CWCB * handler, void *handler_data)
{
    comm_write(fd, mb.buf, mb.size, handler, handler_data, memBufFreeFunc(&mb));
}

/* a wrapper around comm_write to allow for MemBuf to be comm_written in a snap */
void
comm_write_mbuf_header(int fd, MemBuf mb, const char *header, size_t header_size, CWCB * handler, void *handler_data)
{
    comm_write_header(fd, mb.buf, mb.size, header, header_size, handler, handler_data, memBufFreeFunc(&mb));
}

/*
 * hm, this might be too general-purpose for all the places we'd
 * like to use it.
 */
int
ignoreErrno(int ierrno)
{
    switch (ierrno) {
    case EINPROGRESS:
    case EWOULDBLOCK:
#if EAGAIN != EWOULDBLOCK
    case EAGAIN:
#endif
    case EALREADY:
    case EINTR:
#ifdef ERESTART
    case ERESTART:
#endif
	return 1;
    default:
	return 0;
    }
    /* NOTREACHED */
}

void
commCloseAllSockets(void)
{
    int fd;
    fde *F = NULL;
    PF *callback;
    for (fd = 0; fd <= Biggest_FD; fd++) {
	F = &fd_table[fd];
	if (!F->flags.open)
	    continue;
	if (F->type != FD_SOCKET)
	    continue;
	if (F->flags.ipc)	/* don't close inter-process sockets */
	    continue;
	if (F->timeout_handler) {
	    debug(5, 5) ("commCloseAllSockets: FD %d: Calling timeout handler\n",
		fd);
	    callback = F->timeout_handler;
	    F->timeout_handler = NULL;
	    callback(fd, F->timeout_data);
	} else {
	    debug(5, 5) ("commCloseAllSockets: FD %d: calling comm_close()\n", fd);
	    comm_close(fd);
	}
    }
}

int
comm_create_fifopair(int *prfd, int *pwfd, int *crfd, int *cwfd)
{
        int p2c[2];
        int c2p[2];

        if (pipe(p2c) < 0) {
            debug(54, 0) ("comm_create_fifopair: pipe: %s\n", xstrerror());
            return -1;
        }

        if (pipe(c2p) < 0) {
            debug(54, 0) ("comm_create_fifopair: pipe: %s\n", xstrerror());
	    close(p2c[0]);
	    close(p2c[1]);
            return -1;
        }

        fd_open(*prfd = p2c[0], FD_PIPE, "IPC FIFO Parent Read");
        fd_open(*cwfd = p2c[1], FD_PIPE, "IPC FIFO Child Write");
        fd_open(*crfd = c2p[0], FD_PIPE, "IPC FIFO Child Read");
        fd_open(*pwfd = c2p[1], FD_PIPE, "IPC FIFO Parent Write");
        sqinet_init(&fd_table[*prfd].local_address);
        sqinet_init(&fd_table[*cwfd].local_address);
        sqinet_init(&fd_table[*crfd].local_address);
        sqinet_init(&fd_table[*pwfd].local_address);
        sqinet_init(&fd_table[*prfd].remote_address);
        sqinet_init(&fd_table[*cwfd].remote_address);
        sqinet_init(&fd_table[*crfd].remote_address);
        sqinet_init(&fd_table[*pwfd].remote_address);

	return 1;
}

#ifndef _SQUID_MSWIN_
int
comm_create_unix_stream_pair(int *prfd, int *pwfd, int *crfd, int *cwfd, int buflen)
{
        int fds[2];

        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
            debug(54, 0) ("comm_create_unix_stream_pair: socketpair: %s\n", xstrerror());
            return -1;
        }
        setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, (void *) &buflen, sizeof(buflen));
        setsockopt(fds[0], SOL_SOCKET, SO_RCVBUF, (void *) &buflen, sizeof(buflen));
        setsockopt(fds[1], SOL_SOCKET, SO_SNDBUF, (void *) &buflen, sizeof(buflen));
        setsockopt(fds[1], SOL_SOCKET, SO_RCVBUF, (void *) &buflen, sizeof(buflen));
        fd_open(*prfd = *pwfd = fds[0], FD_PIPE, "IPC UNIX STREAM Parent");
        fd_open(*crfd = *cwfd = fds[1], FD_PIPE, "IPC UNIX STREAM Parent");
        sqinet_init(&fd_table[*prfd].local_address);
        sqinet_init(&fd_table[*crfd].local_address);
        sqinet_init(&fd_table[*prfd].remote_address);
        sqinet_init(&fd_table[*crfd].remote_address);

	return 1;
}

int
comm_create_unix_dgram_pair(int *prfd, int *pwfd, int *crfd, int *cwfd)
{
        int fds[2];
        if (socketpair(AF_UNIX, SOCK_DGRAM, 0, fds) < 0) {
            debug(54, 0) ("comm_create_unix_dgram_pair: socketpair: %s\n", xstrerror());
            return -1;
        }
        fd_open(*prfd = *pwfd = fds[0], FD_PIPE, "IPC UNIX DGRAM Parent");
	fd_open(*crfd = *cwfd = fds[1], FD_PIPE, "IPC UNIX DGRAM Parent");
        sqinet_init(&fd_table[*prfd].local_address);
        sqinet_init(&fd_table[*crfd].local_address);
        sqinet_init(&fd_table[*prfd].remote_address);
        sqinet_init(&fd_table[*crfd].remote_address);

	return 1;
}
#endif
