
/*
 * $Id: ssl.c 12993 2008-06-30 11:36:05Z adrian.chadd $
 *
 * DEBUG: section 26    Secure Sockets Layer Proxy
 * AUTHOR: Duane Wessels
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

#include "include/config.h"

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

#include "include/Array.h"
#include "include/Stack.h"
#include "include/util.h"
#include "libcore/valgrind.h"
#include "libcore/varargs.h"
#include "libcore/debug.h"
#include "libcore/kb.h"
#include "libcore/gb.h"
#include "libcore/tools.h"

#include "libmem/MemPool.h"
#include "libmem/MemBufs.h"
#include "libmem/MemBuf.h"

#include "libcb/cbdata.h"

#include "libsqinet/sqinet.h"

#include "libiapp/iapp_ssl.h"
#include "libiapp/fd_types.h"
#include "libiapp/comm_types.h"
#include "libiapp/comm.h"
#include "libiapp/pconn_hist.h"
#include "libiapp/mainloop.h"

#include "tunnel.h"

static CNCB sslConnectDone;
static PF sslServerClosed;
static PF sslClientClosed;
static PF sslReadClient;
static PF sslReadServer;
static PF sslTimeout;
static PF sslWriteClient;
static PF sslWriteServer;
static void sslStateFree(SslStateData * sslState);
static void sslConnected(int fd, void *);
static void sslSetSelect(SslStateData * sslState);

static void
sslAbort(SslStateData * sslState)
{
    debug(26, 3) ("sslAbort: FD %d/%d\n", sslState->client.fd, sslState->server.fd);
    cbdataLock(sslState);
    if (sslState->client.fd > -1)
	comm_close(sslState->client.fd);
    if (sslState->server.fd > -1)
	comm_close(sslState->server.fd);
    cbdataUnlock(sslState);
}

static void
sslServerClosed(int fd, void *data)
{
    SslStateData *sslState = data;
    debug(26, 3) ("sslServerClosed: FD %d\n", fd);
    assert(fd == sslState->server.fd);
    sslState->server.fd = -1;
    if (sslState->client.fd == -1)
	sslStateFree(sslState);
}

static void
sslClientClosed(int fd, void *data)
{
    SslStateData *sslState = data;
    debug(26, 3) ("sslClientClosed: FD %d\n", fd);
    assert(fd == sslState->client.fd);
    sslState->client.fd = -1;
    if (sslState->server.fd == -1)
	sslStateFree(sslState);
    else if (!sslState->connected)
	comm_close(sslState->server.fd);
}

static void
sslStateFree(SslStateData * sslState)
{
    debug(26, 3) ("sslStateFree: sslState=%p\n", sslState);
    assert(sslState != NULL);
    assert(sslState->client.fd == -1);
    assert(sslState->server.fd == -1);
    safe_free(sslState->server.buf);
    safe_free(sslState->client.buf);
    sqinet_done(&sslState->peer);
    cbdataFree(sslState);
}

static void
sslSetSelect(SslStateData * sslState)
{
    size_t read_sz = SQUID_TCP_SO_RCVBUF;
    assert(sslState->server.fd > -1 || sslState->client.fd > -1);
    if (sslState->client.fd > -1) {
	if (sslState->server.len > 0) {
	    commSetSelect(sslState->client.fd,
		COMM_SELECT_WRITE,
		sslWriteClient,
		sslState,
		0);
	}
	if (sslState->client.len < read_sz) {
	    commSetSelect(sslState->client.fd,
		COMM_SELECT_READ,
		sslReadClient,
		sslState,
		300);
	}
    } else if (sslState->client.len == 0 && sslState->server.fd > -1) {
	comm_close(sslState->server.fd);
    }
    if (!sslState->connected) {
	/* Not yet connected. wait.. */
    } else if (sslState->server.fd > -1) {
	if (sslState->client.len > 0) {
	    commSetSelect(sslState->server.fd,
		COMM_SELECT_WRITE,
		sslWriteServer,
		sslState,
		0);
	}
	if (sslState->server.len < read_sz) {
	    /* Have room to read more */
	    commSetSelect(sslState->server.fd,
		COMM_SELECT_READ,
		sslReadServer,
		sslState,
		300);
	}
    } else if (sslState->server.len == 0 && sslState->client.fd > -1) {
	comm_close(sslState->client.fd);
    }
}

/* Read from server side and queue it for writing to the client */
static void
sslReadServer(int fd, void *data)
{
    SslStateData *sslState = data;
    int len;
    size_t read_sz = SQUID_TCP_SO_RCVBUF - sslState->server.len;
    assert(fd == sslState->server.fd);
    debug(26, 3) ("sslReadServer: FD %d, reading %d bytes at offset %d\n",
	fd, (int) read_sz, sslState->server.len);
    errno = 0;
    CommStats.syscalls.sock.reads++;
    len = FD_READ_METHOD(fd, sslState->server.buf + sslState->server.len, read_sz);
    debug(26, 3) ("sslReadServer: FD %d, read   %d bytes\n", fd, len);
    if (len > 0) {
	fd_bytes(fd, len, FD_READ);
#if NOTYET
	kb_incr(&statCounter.server.all.kbytes_in, len);
	kb_incr(&statCounter.server.other.kbytes_in, len);
#endif
	sslState->server.len += len;
    }
    cbdataLock(sslState);
    if (len < 0) {
	int level = 1;
#ifdef ECONNRESET
	if (errno == ECONNRESET)
	    level = 2;
#endif
	if (ignoreErrno(errno))
	    level = 3;
	debug(50, level) ("sslReadServer: FD %d: read failure: %s\n", fd, xstrerror());
	if (!ignoreErrno(errno))
	    comm_close(fd);
    } else if (len == 0) {
	comm_close(fd);
    }
    if (cbdataValid(sslState))
	sslSetSelect(sslState);
    cbdataUnlock(sslState);
}

/* Read from client side and queue it for writing to the server */
static void
sslReadClient(int fd, void *data)
{
    SslStateData *sslState = data;
    int len;
    assert(fd == sslState->client.fd);
    debug(26, 3) ("sslReadClient: FD %d, reading %d bytes at offset %d\n",
	fd, SQUID_TCP_SO_RCVBUF - sslState->client.len,
	sslState->client.len);
    CommStats.syscalls.sock.reads++;
    len = FD_READ_METHOD(fd,
	sslState->client.buf + sslState->client.len,
	SQUID_TCP_SO_RCVBUF - sslState->client.len);
    debug(26, 3) ("sslReadClient: FD %d, read   %d bytes\n", fd, len);
    if (len > 0) {
	fd_bytes(fd, len, FD_READ);
#if NOTYET
	kb_incr(&statCounter.client_http.kbytes_in, len);
#endif
	sslState->client.len += len;
    }
    cbdataLock(sslState);
    if (len < 0) {
	int level = 1;
#ifdef ECONNRESET
	if (errno == ECONNRESET)
	    level = 2;
#endif
	if (ignoreErrno(errno))
	    level = 3;
	debug(50, level) ("sslReadClient: FD %d: read failure: %s\n",
	    fd, xstrerror());
	if (!ignoreErrno(errno))
	    sslAbort(sslState);
    } else if (len == 0) {
	comm_close(fd);
    }
    if (cbdataValid(sslState))
	sslSetSelect(sslState);
    cbdataUnlock(sslState);
}

/* Writes data from the client buffer to the server side */
static void
sslWriteServer(int fd, void *data)
{
    SslStateData *sslState = data;
    int len;
    assert(fd == sslState->server.fd);
    debug(26, 3) ("sslWriteServer: FD %d, %d bytes to write\n",
	fd, sslState->client.len);
    CommStats.syscalls.sock.writes++;
    len = FD_WRITE_METHOD(fd,
	sslState->client.buf,
	sslState->client.len);
    debug(26, 3) ("sslWriteServer: FD %d, %d bytes written\n", fd, len);
    if (len > 0) {
	fd_bytes(fd, len, FD_WRITE);
#if 0
	kb_incr(&statCounter.server.all.kbytes_out, len);
	kb_incr(&statCounter.server.other.kbytes_out, len);
#endif
	/* increment total object size */
	assert(len <= sslState->client.len);
	sslState->client.len -= len;
	if (sslState->client.len > 0) {
	    /* we didn't write the whole thing */
	    xmemmove(sslState->client.buf,
		sslState->client.buf + len,
		sslState->client.len);
	}
    }
    cbdataLock(sslState);
    if (len < 0) {
	debug(50, ignoreErrno(errno) ? 3 : 1)
	    ("sslWriteServer: FD %d: write failure: %s.\n", fd, xstrerror());
	if (!ignoreErrno(errno))
	    sslAbort(sslState);
    }
    if (cbdataValid(sslState))
	sslSetSelect(sslState);
    cbdataUnlock(sslState);
}

/* Writes data from the server buffer to the client side */
static void
sslWriteClient(int fd, void *data)
{
    SslStateData *sslState = data;
    int len;
    assert(fd == sslState->client.fd);
    debug(26, 3) ("sslWriteClient: FD %d, %d bytes to write\n",
	fd, sslState->server.len);
    CommStats.syscalls.sock.writes++;
    len = FD_WRITE_METHOD(fd,
	sslState->server.buf,
	sslState->server.len);
    debug(26, 3) ("sslWriteClient: FD %d, %d bytes written\n", fd, len);
    if (len > 0) {
	fd_bytes(fd, len, FD_WRITE);
#if 0
	kb_incr(&statCounter.client_http.kbytes_out, len);
#endif
	assert(len <= sslState->server.len);
	sslState->server.len -= len;
	/* increment total object size */
	if (sslState->server.len > 0) {
	    /* we didn't write the whole thing */
	    xmemmove(sslState->server.buf,
		sslState->server.buf + len,
		sslState->server.len);
	}
    }
    cbdataLock(sslState);
    if (len < 0) {
	debug(50, ignoreErrno(errno) ? 3 : 1)
	    ("sslWriteClient: FD %d: write failure: %s.\n", fd, xstrerror());
	if (!ignoreErrno(errno))
	    sslAbort(sslState);
    }
    if (cbdataValid(sslState))
	sslSetSelect(sslState);
    cbdataUnlock(sslState);
}

static void
sslTimeout(int fd, void *data)
{
    SslStateData *sslState = data;
    debug(26, 3) ("sslTimeout: FD %d\n", fd);
    sslAbort(sslState);
}

static void
sslConnected(int fd, void *data)
{
    SslStateData *sslState = data;
    debug(26, 3) ("sslConnected: FD %d sslState=%p\n", fd, sslState);
    sslSetSelect(sslState);
}


static void
sslConnectDone(int fd, int status, void *data)
{
    SslStateData *sslState = data;
    if (status != COMM_OK) {
	debug(26, 3) ("commConnectDone: %p: FD %d: failure! errno %d\n", sslState, fd, errno);
	comm_close(fd);
	comm_close(sslState->client.fd);
    } else {
	sslState->connected = 1;
	sslConnected(sslState->server.fd, sslState);
	commSetTimeout(sslState->server.fd,
	    300,
	    sslTimeout,
	    sslState);
    }
}

static void
sslConnectTimeout(int fd, void *data)
{
    SslStateData *sslState = data;
    comm_close(fd);
    comm_close(sslState->client.fd);
}

static void
sslConnectHandle(int fd, void *data)
{
    int ret;

    SslStateData *sslState = data;

    debug(26, 3) ("sslConnectHandle: FD %d: %p: trying\n", fd, sslState);
    ret = comm_connect_addr(sslState->server.fd, &sslState->peer);
    debug(26, 3) ("sslConnectHandle: FD %d: %p: comm_connect_addr returned %d\n", fd, sslState, ret);
    if (ret == COMM_INPROGRESS) {
        debug(26, 3) ("sslConnectHandle: FD %d: %p: re-scheduling for connect completion\n", fd, sslState);
    	commSetSelect(sslState->server.fd, COMM_SELECT_WRITE, sslConnectHandle, sslState, 0);
	return;
    }
    sslConnectDone(fd, ret, sslState);
}

CBDATA_TYPE(SslStateData);
void
sslStart(int fd, sqaddr_t *peer)
{
    /* Create state structure. */
    SslStateData *sslState = NULL;
    int sock;
    struct in_addr outgoing;
    unsigned long tos = 0;
    debug(26, 3) ("sslStart: %d'\n", fd);
#if 0
    statCounter.server.all.requests++;
    statCounter.server.other.requests++;
#endif
#if 0
    outgoing = getOutgoingAddr(request);
    tos = getOutgoingTOS(request);
#endif
    /* Create socket. */
    bzero(&outgoing, sizeof(outgoing));
    sock = comm_open(SOCK_STREAM, IPPROTO_TCP, outgoing, 0, COMM_NONBLOCKING, tos, NULL);
    if (sock == COMM_ERROR) {
	debug(26, 4) ("sslStart: Failed because we're out of sockets.\n");
	comm_close(fd);
	return;
    }
    CBDATA_INIT_TYPE(SslStateData);
    sslState = cbdataAlloc(SslStateData);
    sqinet_copy(&sslState->peer, peer);
    sslState->client.fd = fd;
    sslState->server.fd = sock;
    sslState->server.buf = xmalloc(SQUID_TCP_SO_RCVBUF);
    sslState->client.buf = xmalloc(SQUID_TCP_SO_RCVBUF);
    /* Copy any pending data from the client connection */
    sslState->client.len = 0;
    comm_add_close_handler(sslState->server.fd,
	sslServerClosed,
	sslState);
    comm_add_close_handler(sslState->client.fd,
	sslClientClosed,
	sslState);
    commSetTimeout(sslState->client.fd,
	300,
	sslTimeout,
	sslState);
    sslSetSelect(sslState);
    commSetTimeout(sslState->server.fd,
	300,
	sslConnectTimeout,
	sslState);
    /* We do this manually for now as there's no async connect() support in libiapp yet */
    comm_connect_begin(sslState->server.fd, &sslState->peer, sslConnectDone, sslState);
}

