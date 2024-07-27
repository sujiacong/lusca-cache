
/*
 * $Id: ipc.c 14492 2010-03-25 03:34:17Z adrian.chadd $
 *
 * DEBUG: section 54    Interprocess Communication
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
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
#include "../libcore/kb.h"
#include "../libcore/gb.h"
#include "../libcore/tools.h"

#include "../libsqdebug/debug.h"
#include "../libsqdebug/debug_file.h"

#include "../libmem/MemPool.h"
#include "../libmem/MemBufs.h"
#include "../libmem/MemBuf.h"

#include "../libcb/cbdata.h"

#include "../libsqinet/sqinet.h"

#include "../libiapp/iapp_ssl.h"
#include "../libiapp/fd_types.h"
#include "../libiapp/comm_types.h"
#include "../libiapp/comm.h"

#include "ipc.h"

static const char *hello_string = "hi there\n";
#define HELLO_BUF_SZ 11
static char hello_buf[HELLO_BUF_SZ];

/* XXX */
extern void no_suid(void);

static int
ipcCloseAllFD(int prfd, int pwfd, int crfd, int cwfd)
{
    if (prfd >= 0)
	comm_close(prfd);
    if (prfd != pwfd)
	if (pwfd >= 0)
	    comm_close(pwfd);
    if (crfd >= 0)
	comm_close(crfd);
    if (crfd != cwfd)
	if (cwfd >= 0)
	    comm_close(cwfd);
    return -1;
}


/*
 * Some issues with this routine at the moment!
 *
 * + All of the FDs are comm_close()'ed but they're not all created via the comm layer!
 *   Gah, etc; so they're "faked" enough for now
 * + This does direct fiddling of debug_log for child processes (so logging works in case
 *   exec() fails) AND it dup's the debug_log FD directly. This means it gets its grubby
 *   hands direct into the debug file code which is probably not a great idea.
 */
pid_t
ipcCreate(int type, const char *prog, const char *const args[], const char *name, int sleep_after_fork, int *rfd, int *wfd, void **hIpc)
{
    pid_t pid;
    sqaddr_t CS;
    sqaddr_t PS;
    int crfd = -1;
    int prfd = -1;
    int cwfd = -1;
    int pwfd = -1;
    int fd;
    int t1, t2, t3;
    socklen_t len;
    int tmp_s;
#if HAVE_PUTENV
    char *env_str;
#endif
    int x;
    LOCAL_ARRAY(char, tmp, MAX_IPSTRLEN);

#if HAVE_POLL && defined(_SQUID_OSF_)
    assert(type != IPC_FIFO);
#endif

#if NOTYET
    requirePathnameExists(name, prog);
#endif

    if (rfd)
	*rfd = -1;
    if (wfd)
	*wfd = -1;
    if (hIpc)
	*hIpc = NULL;

    if (type == IPC_TCP_SOCKET) {
	crfd = cwfd = comm_open(SOCK_STREAM,
	    IPPROTO_TCP,
	    local_addr,
	    0,
	    COMM_NOCLOEXEC,
	    COMM_TOS_DEFAULT,
	    name);
	prfd = pwfd = comm_open(SOCK_STREAM,
	    IPPROTO_TCP,	/* protocol */
	    local_addr,
	    0,			/* port */
	    0,			/* blocking */
	    COMM_TOS_DEFAULT,
	    name);
    } else if (type == IPC_UDP_SOCKET) {
	crfd = cwfd = comm_open(SOCK_DGRAM,
	    IPPROTO_UDP,
	    local_addr,
	    0,
	    COMM_NOCLOEXEC,
	    COMM_TOS_DEFAULT,
	    name);
	prfd = pwfd = comm_open(SOCK_DGRAM,
	    IPPROTO_UDP,
	    local_addr,
	    0,
	    0,
	    COMM_TOS_DEFAULT,
	    name);
    } else if (type == IPC_FIFO) {
        if (! comm_create_fifopair(&prfd, &pwfd, &crfd, &cwfd)) {
	    debugs(54, 0, "ipcCreate: pipe: %s", xstrerror());
        }
    } else if (type == IPC_UNIX_STREAM) {
        if (! comm_create_unix_stream_pair(&prfd, &pwfd, &crfd, &cwfd, 32768)) {
	    debugs(54, 0, "ipcCreate: pipe: %s", xstrerror());
        }
    } else if (type == IPC_UNIX_DGRAM) {
        if (! comm_create_unix_dgram_pair(&prfd, &pwfd, &crfd, &cwfd)) {
            debugs(54, 0, "ipcCreate: dgrampair: %s", xstrerror());
            return -1;
        }
    } else {
	assert(IPC_NONE);
    }
    debugs(54, 3, "ipcCreate: prfd FD %d", prfd);
    debugs(54, 3, "ipcCreate: pwfd FD %d", pwfd);
    debugs(54, 3, "ipcCreate: crfd FD %d", crfd);
    debugs(54, 3, "ipcCreate: cwfd FD %d", cwfd);

    if (crfd < 0) {
	debugs(54, 0, "ipcCreate: Failed to create child FD.");
	return ipcCloseAllFD(prfd, pwfd, crfd, cwfd);
    }
    if (pwfd < 0) {
	debugs(54, 0, "ipcCreate: Failed to create server FD.");
	return ipcCloseAllFD(prfd, pwfd, crfd, cwfd);
    }
    sqinet_init(&PS);
    sqinet_init(&CS);

    if (type == IPC_TCP_SOCKET || type == IPC_UDP_SOCKET) {
	len = sqinet_get_maxlength(&PS);
	if (getsockname(pwfd, sqinet_get_entry(&PS), &len) < 0) {
	    debugs(54, 0, "ipcCreate: getsockname: %s", xstrerror());
	    sqinet_done(&PS);
	    sqinet_done(&CS);
	    return ipcCloseAllFD(prfd, pwfd, crfd, cwfd);
	}
        sqinet_ntoa(&PS, tmp, MAX_IPSTRLEN, 0);
	debugs(54, 3, "ipcCreate: FD %d sockaddr %s:%d",
	    pwfd, tmp, sqinet_get_port(&PS));
	len = sqinet_get_maxlength(&CS);
	if (getsockname(crfd, sqinet_get_entry(&CS), &len) < 0) {
	    debugs(54, 0, "ipcCreate: getsockname: %s", xstrerror());
	    sqinet_done(&PS);
	    sqinet_done(&CS);
	    return ipcCloseAllFD(prfd, pwfd, crfd, cwfd);
	}
        sqinet_ntoa(&CS, tmp, MAX_IPSTRLEN, 0);
	debugs(54, 3, "ipcCreate: FD %d sockaddr %s:%d",
	    crfd, tmp, sqinet_get_port(&CS));
    }
    if (type == IPC_TCP_SOCKET) {
	if (listen(crfd, 1) < 0) {
	    debugs(54, 1, "ipcCreate: listen FD %d: %s", crfd, xstrerror());
	    sqinet_done(&PS);
	    sqinet_done(&CS);
	    return ipcCloseAllFD(prfd, pwfd, crfd, cwfd);
	}
	debugs(54, 3, "ipcCreate: FD %d listening...", crfd);
    }
    /* flush or else we get dup data if unbuffered_logs is set */
    logsFlush();
    if ((pid = fork()) < 0) {
	debugs(54, 1, "ipcCreate: fork: %s", xstrerror());
	return ipcCloseAllFD(prfd, pwfd, crfd, cwfd);
    }
    if (pid > 0) {		/* parent */
	/* close shared socket with child */
	comm_close(crfd);
	if (cwfd != crfd)
	    comm_close(cwfd);
	cwfd = crfd = -1;
	if (type == IPC_TCP_SOCKET || type == IPC_UDP_SOCKET) {
	    if (comm_connect_addr(pwfd, &CS) == COMM_ERROR) {
	        sqinet_done(&PS);
	        sqinet_done(&CS);
		return ipcCloseAllFD(prfd, pwfd, crfd, cwfd);
	    }
	}
	memset(hello_buf, '\0', HELLO_BUF_SZ);
	if (type == IPC_UDP_SOCKET)
	    x = recv(prfd, hello_buf, HELLO_BUF_SZ - 1, 0);
	else
	    x = read(prfd, hello_buf, HELLO_BUF_SZ - 1);
	if (x < 0) {
	    debugs(54, 0, "ipcCreate: PARENT: hello read test failed");
	    debugs(54, 0, "--> read: %s", xstrerror());
	    sqinet_done(&PS);
	    sqinet_done(&CS);
	    return ipcCloseAllFD(prfd, pwfd, crfd, cwfd);
	} else if (strcmp(hello_buf, hello_string)) {
	    debugs(54, 0, "ipcCreate: PARENT: hello read test failed");
	    debugs(54, 0, "--> read returned %d", x);
	    debugs(54, 0, "--> got '%s'", rfc1738_escape(hello_buf));
	    sqinet_done(&PS);
	    sqinet_done(&CS);
	    return ipcCloseAllFD(prfd, pwfd, crfd, cwfd);
	}
	debugs(54, 2, "ipcCreate: hello read: %d bytes", x);
	commSetTimeout(prfd, -1, NULL, NULL);
	commSetNonBlocking(prfd);
	commSetNonBlocking(pwfd);
	commSetCloseOnExec(prfd);
	commSetCloseOnExec(pwfd);
	if (rfd)
	    *rfd = prfd;
	if (wfd)
	    *wfd = pwfd;
	fd_table[prfd].flags.ipc = 1;
	fd_table[pwfd].flags.ipc = 1;
	if (sleep_after_fork)
	    xusleep(sleep_after_fork);
	sqinet_done(&PS);
	sqinet_done(&CS);
	return pid;
    }
    /* child */
    no_suid();			/* give up extra priviliges */
    /* close shared socket with parent */
    close(prfd);
    if (pwfd != prfd)
	close(pwfd);
    pwfd = prfd = -1;

    if (type == IPC_TCP_SOCKET) {
	debugs(54, 3, "ipcCreate: calling accept on FD %d", crfd);
	if ((fd = accept(crfd, NULL, NULL)) < 0) {
	    debugs(54, 0, "ipcCreate: FD %d accept: %s", crfd, xstrerror());
	    sqinet_done(&PS);
	    sqinet_done(&CS);
	    _exit(1);
	}
	debugs(54, 3, "ipcCreate: CHILD accepted new FD %d", fd);
	close(crfd);
	cwfd = crfd = fd;
    } else if (type == IPC_UDP_SOCKET) {
	if (comm_connect_addr(crfd, &PS) == COMM_ERROR) {
	    sqinet_done(&PS);
	    sqinet_done(&CS);
	    return ipcCloseAllFD(prfd, pwfd, crfd, cwfd);
	}
    }
    if (type == IPC_UDP_SOCKET) {
	x = send(cwfd, hello_string, strlen(hello_string) + 1, 0);
	if (x < 0) {
	    debugs(54, 0, "sendto FD %d: %s", cwfd, xstrerror());
	    debugs(54, 0, "ipcCreate: CHILD: hello write test failed");
	    sqinet_done(&PS);
	    sqinet_done(&CS);
	    _exit(1);
	}
    } else {
	if (write(cwfd, hello_string, strlen(hello_string) + 1) < 0) {
	    debugs(54, 0, "write FD %d: %s", cwfd, xstrerror());
	    debugs(54, 0, "ipcCreate: CHILD: hello write test failed");
	    sqinet_done(&PS);
	    sqinet_done(&CS);
	    _exit(1);
	}
    }
#if HAVE_PUTENV
    if (_debug_options) {
        env_str = xcalloc((tmp_s = strlen(_debug_options) + 32), 1);
        snprintf(env_str, tmp_s, "SQUID_DEBUG=%s", _debug_options);
        putenv(env_str);
    }
#endif
    /*
     * This double-dup stuff avoids problems when one of 
     *  crfd, cwfd, or debug_log are in the rage 0-2.
     */
    do {
	/* First make sure 0-2 is occupied by something. Gets cleaned up later */
	x = dup(crfd);
	assert(x > -1);
    } while (x < 3 && x > -1);
    close(x);
    sqinet_done(&PS);
    sqinet_done(&CS);
    t1 = dup(crfd);
    t2 = dup(cwfd);
    t3 = dup(fileno(debug_log));
    assert(t1 > 2 && t2 > 2 && t3 > 2);
    close(crfd);
    close(cwfd);
    close(fileno(debug_log));
    dup2(t1, 0);
    dup2(t2, 1);
    dup2(t3, 2);
    close(t1);
    close(t2);
    close(t3);
#if 0
    /* Make sure all other filedescriptors are closed */
    for (x = 3; x < Squid_MaxFD; x++)
	close(x);
#endif
#if HAVE_SETSID
    setsid();
#endif
    execvp(prog, (char *const *) args);
    debug_log = fdopen(2, "a+");

    debugs(54, 0, "ipcCreate: %s: %s", prog, xstrerror());
    _exit(1);
    return 0;
}

void
ipcClose(pid_t pid, int rfd, int wfd)
{
	if (rfd == wfd)
		comm_close(rfd);
	else {
		comm_close(rfd);
		comm_close(wfd);
	}
	kill(pid, SIGTERM);
}
