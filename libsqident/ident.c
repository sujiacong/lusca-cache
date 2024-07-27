
/*
 * $Id: ident.c 14486 2010-03-24 14:58:37Z adrian.chadd $
 *
 * DEBUG: section 30    Ident (RFC 931)
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

#include "../include/config.h"

#if USE_IDENT

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <ctype.h>
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
#include "../include/hash.h"
#include "../include/rfc1035.h"

#include "../libcore/valgrind.h"
#include "../libcore/varargs.h"
#include "../libcore/debug.h"
#include "../libcore/kb.h"
#include "../libcore/gb.h"
#include "../libcore/tools.h"
#include "../libcore/dlink.h"

#include "../libmem/MemPool.h"
#include "../libmem/MemBufs.h"
#include "../libmem/MemBuf.h"

#include "../libcb/cbdata.h"

#include "../libsqinet/sqinet.h"

#include "../libiapp/iapp_ssl.h"
#include "../libiapp/fd_types.h"
#include "../libiapp/comm_types.h"
#include "../libiapp/comm.h"

#include "ident.h"

#define IDENT_PORT 113
#define IDENT_KEY_SZ (MAX_IPSTRLEN*2)

typedef struct _IdentClient {
    IDCB *callback;
    void *callback_data;
    struct _IdentClient *next;
} IdentClient;

typedef struct _IdentStateData {
    hash_link hash;		/* must be first */
    int fd;			/* IDENT fd */
    sqaddr_t me;
    sqaddr_t my_peer;
    IdentClient *clients;
} IdentStateData;

static PF identReadReply;
static PF identClose;
static PF identTimeout;
static CNCB identConnectDone;
static hash_table *ident_hash = NULL;
static void identClientAdd(IdentStateData *, IDCB *, void *);

static int ident_timeout = 30;

/**** PRIVATE FUNCTIONS ****/

static void
identCallback(IdentStateData * state, char *result)
{
    IdentClient *client;
    if (result && *result == '\0')
	result = NULL;
    while ((client = state->clients)) {
	state->clients = client->next;
	if (cbdataValid(client->callback_data))
	    client->callback(result, client->callback_data);
	cbdataUnlock(client->callback_data);
	xfree(client);
    }
}

static void
identClose(int fdnotused, void *data)
{
    IdentStateData *state = data;
    identCallback(state, NULL);
    comm_close(state->fd);
    hash_remove_link(ident_hash, (hash_link *) state);
    safe_free(state->hash.key);
    sqinet_done(&state->me);
    sqinet_done(&state->my_peer);
    cbdataFree(state);
}

static void
identTimeout(int fd, void *data)
{
    IdentStateData *state = data;
    LOCAL_ARRAY(char, buf, MAX_IPSTRLEN);
    (void) sqinet_ntoa(&state->my_peer, buf, sizeof(buf), SQADDR_NONE);
    debugs(30, 3, "identTimeout: FD %d: %s", fd, buf);
    comm_close(fd);
}

static void
identConnectDone(int fd, int status, void *data)
{
    IdentStateData *state = data;
    IdentClient *c;
    MemBuf mb;
    if (status != COMM_OK) {
	/* Failed to connect */
	comm_close(fd);
	return;
    }
    /*
     * see if our clients still care
     */
    for (c = state->clients; c; c = c->next) {
	if (cbdataValid(c->callback_data))
	    break;
    }
    if (c == NULL) {
	/* no clients care */
	comm_close(fd);
	return;
    }
    memBufDefInit(&mb);
    memBufPrintf(&mb, "%d, %d\r\n",
        sqinet_get_port(&state->my_peer), sqinet_get_port(&state->me));
    comm_write_mbuf(fd, mb, NULL, state);
    commSetSelect(fd, COMM_SELECT_READ, identReadReply, state, 0);
    commSetTimeout(fd, ident_timeout, identTimeout, state);
}

static void
identReadReply(int fd, void *data)
{
    IdentStateData *state = data;
    LOCAL_ARRAY(char, buf, BUFSIZ);
    char *ident = NULL;
    char *t = NULL;
    int len = -1;
    buf[0] = '\0';
    CommStats.syscalls.sock.reads++;
    len = FD_READ_METHOD(fd, buf, BUFSIZ - 1);
    fd_bytes(fd, len, FD_READ);
    if (len <= 0) {
	comm_close(fd);
	return;
    }
    /*
     * XXX This isn't really very tolerant. It should read until EOL
     * or EOF and then decode the answer... If the reply is fragmented
     * then this will fail
     */
    buf[len] = '\0';
    if ((t = strchr(buf, '\r')))
	*t = '\0';
    if ((t = strchr(buf, '\n')))
	*t = '\0';
    debugs(30, 5, "identReadReply: FD %d: Read '%s'", fd, buf);
    if (strstr(buf, "USERID")) {
	if ((ident = strrchr(buf, ':'))) {
	    while (xisspace(*++ident));
	    identCallback(state, ident);
	}
    }
    comm_close(fd);
}


static void
identClientAdd(IdentStateData * state, IDCB * callback, void *callback_data)
{
    IdentClient *c = xcalloc(1, sizeof(*c));
    IdentClient **C;
    c->callback = callback;
    c->callback_data = callback_data;
    cbdataLock(callback_data);
    for (C = &state->clients; *C; C = &(*C)->next);
    *C = c;
}

CBDATA_TYPE(IdentStateData);

/**** PUBLIC FUNCTIONS ****/

void
identStart4(struct sockaddr_in *me, struct sockaddr_in *my_peer, IDCB * callback, void *data)
{
	sqaddr_t m, p;
	sqinet_init(&m);
	sqinet_init(&p);
	sqinet_set_v4_sockaddr(&m, me);
	sqinet_set_v4_sockaddr(&p, my_peer);
	identStart(&m, &p, callback, data);
	sqinet_done(&m);
	sqinet_done(&p);
}

/*
 * start a TCP connection to the peer host on port 113
 */
void
identStart(sqaddr_t *me, sqaddr_t *my_peer, IDCB * callback, void *data)
{
    IdentStateData *state;
    int fd;
    LOCAL_ARRAY(char, buf, MAX_IPSTRLEN);
    char key1[IDENT_KEY_SZ];
    char key2[IDENT_KEY_SZ];
    char key[IDENT_KEY_SZ];

    (void) sqinet_ntoa(me, buf, sizeof(buf), SQADDR_NONE);
    snprintf(key1, IDENT_KEY_SZ, "%s:%d",
        buf,
	sqinet_get_port(me));

    (void) sqinet_ntoa(my_peer, buf, sizeof(buf), SQADDR_NONE);
    snprintf(key2, IDENT_KEY_SZ, "%s:%d",
        buf,
	sqinet_get_port(my_peer));
    snprintf(key, IDENT_KEY_SZ, "%s,%s", key1, key2);
    if ((state = hash_lookup(ident_hash, key)) != NULL) {
	identClientAdd(state, callback, data);
	return;
    }
    /* This replicates the previous behaviour - set port to 0; connect() will figure it out? */
    /* XXX need to verify that the sqaddr gets the right local port details on connect()! */
    CBDATA_INIT_TYPE(IdentStateData);
    state = cbdataAlloc(IdentStateData);
    state->hash.key = xstrdup(key);
    sqinet_init(&state->me);
    sqinet_copy(&state->me, me);
    sqinet_init(&state->my_peer);
    sqinet_copy(&state->my_peer, my_peer);

    sqinet_set_port(&state->me, 0, SQADDR_NONE);
    fd = comm_open6(SOCK_STREAM,
	IPPROTO_TCP,
	&state->me,
	COMM_NONBLOCKING,
	COMM_TOS_DEFAULT,
	"ident");
    if (fd == COMM_ERROR) {
	/* Failed to get a local socket */
        cbdataFree(state);
	callback(NULL, data);
	return;
    }
    state->fd = fd;
    identClientAdd(state, callback, data);
    hash_join(ident_hash, &state->hash);
    comm_add_close_handler(fd,
	identClose,
	state);
    commSetTimeout(fd, ident_timeout, identTimeout, state);
    comm_connect_begin(fd, &state->my_peer, identConnectDone, state);
}

void
identInit(void)
{
    ident_hash = hash_create((HASHCMP *) strcmp,
	hashPrime(Squid_MaxFD / 8),
	hash4);
}

void
identConfigTimeout(int timeout)
{
	ident_timeout = timeout;
}

#endif
