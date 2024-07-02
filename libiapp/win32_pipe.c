
/*
 * $Id: win32lib.c 14534 2010-04-03 13:47:46Z adrian.chadd $
 *
 * Windows support
 * AUTHOR: Guido Serassio <serassio@squid-cache.org>
 * inspired by previous work by Romeo Anghelache & Eric Stern.
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

/* The following code section is part of the native Windows Squid port */
#if defined(_SQUID_MSWIN_)

#undef strerror
#define sys_nerr _sys_nerr

#undef assert
#include <assert.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <windows.h>
#include <string.h>
#include <sys/timeb.h>
#if HAVE_WIN32_PSAPI
#include <psapi.h>
#endif

#include "../include/util.h"
#include "../include/win32_error.h"
#include "../include/win32_version.h"

#include "../include/Array.h"
#include "../include/Stack.h"

#include "../libcore/kb.h"
#include "../libcore/gb.h"

#include "../libsqinet/sqinet.h"

#include "../libmem/MemPool.h"
#include "../libmem/MemBufs.h"
#include "../libmem/MemBuf.h"

#include "fd_types.h"
#include "comm_types.h"
#include "comm.h"

/* internal to Microsoft CRTLIB */
#define FPIPE           0x08	/* file handle refers to a pipe */
typedef struct {
    long osfhnd;		/* underlying OS file HANDLE */
    char osfile;		/* attributes of file (e.g., open in text mode?) */
    char pipech;		/* one char buffer for handles opened on pipes */
#ifdef _MT
    int lockinitflag;
    CRITICAL_SECTION lock;
#endif				/* _MT */
} ioinfo;

#define IOINFO_L2E          5
#define IOINFO_ARRAY_ELTS   (1 << IOINFO_L2E)
#define _pioinfo(i) ( __pioinfo[(i) >> IOINFO_L2E] + ((i) & (IOINFO_ARRAY_ELTS - 1)) )
#define _osfile(i)  ( _pioinfo(i)->osfile )
#define _osfhnd(i)  ( _pioinfo(i)->osfhnd )

#if defined(_MSC_VER)		/* Microsoft C Compiler ONLY */

extern _CRTIMP ioinfo *__pioinfo[];
int __cdecl _free_osfhnd(int);
#define FOPEN           0x01	/* file handle open */

#elif defined(__MINGW32__)	/* MinGW environment */

#define FOPEN           0x01	/* file handle open */
__MINGW_IMPORT ioinfo *__pioinfo[];
int _free_osfhnd(int);

#endif

#if defined(_SQUID_MSWIN_)
int
WIN32_pipe(int handles[2])
{
    int new_socket;
    fde *F = NULL;

    struct sockaddr_in serv_addr;
    int len = sizeof(serv_addr);
    u_short handle1_port;

    handles[0] = handles[1] = -1;

    CommStats.syscalls.sock.sockets++;
    if ((new_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	return -1;

    memset((void *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(0);
    serv_addr.sin_addr = local_addr;

    if (bind(new_socket, (SOCKADDR *) & serv_addr, len) < 0 ||
	listen(new_socket, 1) < 0 || getsockname(new_socket, (SOCKADDR *) & serv_addr, &len) < 0 ||
	(handles[1] = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
	closesocket(new_socket);
	return -1;
    }
    handle1_port = ntohs(serv_addr.sin_port);
    if (connect(handles[1], (SOCKADDR *) & serv_addr, len) < 0 ||
	(handles[0] = accept(new_socket, (SOCKADDR *) & serv_addr, &len)) < 0) {
	closesocket(handles[1]);
	handles[1] = -1;
	closesocket(new_socket);
	return -1;
    }
    closesocket(new_socket);

    /* XXX fix! */
    F = &fd_table[handles[0]];
    sqinet_init(&F->local_address);
    sqinet_set_v4_inaddr(&F->local_address, &local_addr);
    F->local_port = ntohs(serv_addr.sin_port);

    F = &fd_table[handles[1]];
    sqinet_init(&F->local_address);
    sqinet_set_v4_inaddr(&F->local_address, &local_addr);
    xstrncpy(F->ipaddrstr, inet_ntoa(local_addr), 16);
    F->remote_port = handle1_port;

    return 0;
}

#endif /* _SQUID_MSWIN_ */

#endif
