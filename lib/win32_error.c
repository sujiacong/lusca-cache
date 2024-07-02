
/*
 * $Id: win32lib.c 14545 2010-04-06 03:06:53Z adrian.chadd $
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

#include "util.h"

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

#include "win32_compat.h"
#include "win32_error.h"

static struct _wsaerrtext {
    int err;
    const char *errconst;
    const char *errdesc;
} _wsaerrtext[] = {

    {
	WSA_E_CANCELLED, "WSA_E_CANCELLED", "Lookup cancelled."
    },
    {
	WSA_E_NO_MORE, "WSA_E_NO_MORE", "No more data available."
    },
    {
	WSAEACCES, "WSAEACCES", "Permission denied."
    },
    {
	WSAEADDRINUSE, "WSAEADDRINUSE", "Address already in use."
    },
    {
	WSAEADDRNOTAVAIL, "WSAEADDRNOTAVAIL", "Cannot assign requested address."
    },
    {
	WSAEAFNOSUPPORT, "WSAEAFNOSUPPORT", "Address family not supported by protocol family."
    },
    {
	WSAEALREADY, "WSAEALREADY", "Operation already in progress."
    },
    {
	WSAEBADF, "WSAEBADF", "Bad file number."
    },
    {
	WSAECANCELLED, "WSAECANCELLED", "Operation cancelled."
    },
    {
	WSAECONNABORTED, "WSAECONNABORTED", "Software caused connection abort."
    },
    {
	WSAECONNREFUSED, "WSAECONNREFUSED", "Connection refused."
    },
    {
	WSAECONNRESET, "WSAECONNRESET", "Connection reset by peer."
    },
    {
	WSAEDESTADDRREQ, "WSAEDESTADDRREQ", "Destination address required."
    },
    {
	WSAEDQUOT, "WSAEDQUOT", "Disk quota exceeded."
    },
    {
	WSAEFAULT, "WSAEFAULT", "Bad address."
    },
    {
	WSAEHOSTDOWN, "WSAEHOSTDOWN", "Host is down."
    },
    {
	WSAEHOSTUNREACH, "WSAEHOSTUNREACH", "No route to host."
    },
    {
	WSAEINPROGRESS, "WSAEINPROGRESS", "Operation now in progress."
    },
    {
	WSAEINTR, "WSAEINTR", "Interrupted function call."
    },
    {
	WSAEINVAL, "WSAEINVAL", "Invalid argument."
    },
    {
	WSAEINVALIDPROCTABLE, "WSAEINVALIDPROCTABLE", "Invalid procedure table from service provider."
    },
    {
	WSAEINVALIDPROVIDER, "WSAEINVALIDPROVIDER", "Invalid service provider version number."
    },
    {
	WSAEISCONN, "WSAEISCONN", "Socket is already connected."
    },
    {
	WSAELOOP, "WSAELOOP", "Too many levels of symbolic links."
    },
    {
	WSAEMFILE, "WSAEMFILE", "Too many open files."
    },
    {
	WSAEMSGSIZE, "WSAEMSGSIZE", "Message too long."
    },
    {
	WSAENAMETOOLONG, "WSAENAMETOOLONG", "File name is too long."
    },
    {
	WSAENETDOWN, "WSAENETDOWN", "Network is down."
    },
    {
	WSAENETRESET, "WSAENETRESET", "Network dropped connection on reset."
    },
    {
	WSAENETUNREACH, "WSAENETUNREACH", "Network is unreachable."
    },
    {
	WSAENOBUFS, "WSAENOBUFS", "No buffer space available."
    },
    {
	WSAENOMORE, "WSAENOMORE", "No more data available."
    },
    {
	WSAENOPROTOOPT, "WSAENOPROTOOPT", "Bad protocol option."
    },
    {
	WSAENOTCONN, "WSAENOTCONN", "Socket is not connected."
    },
    {
	WSAENOTEMPTY, "WSAENOTEMPTY", "Directory is not empty."
    },
    {
	WSAENOTSOCK, "WSAENOTSOCK", "Socket operation on nonsocket."
    },
    {
	WSAEOPNOTSUPP, "WSAEOPNOTSUPP", "Operation not supported."
    },
    {
	WSAEPFNOSUPPORT, "WSAEPFNOSUPPORT", "Protocol family not supported."
    },
    {
	WSAEPROCLIM, "WSAEPROCLIM", "Too many processes."
    },
    {
	WSAEPROTONOSUPPORT, "WSAEPROTONOSUPPORT", "Protocol not supported."
    },
    {
	WSAEPROTOTYPE, "WSAEPROTOTYPE", "Protocol wrong type for socket."
    },
    {
	WSAEPROVIDERFAILEDINIT, "WSAEPROVIDERFAILEDINIT", "Unable to initialise a service provider."
    },
    {
	WSAEREFUSED, "WSAEREFUSED", "Refused."
    },
    {
	WSAEREMOTE, "WSAEREMOTE", "Too many levels of remote in path."
    },
    {
	WSAESHUTDOWN, "WSAESHUTDOWN", "Cannot send after socket shutdown."
    },
    {
	WSAESOCKTNOSUPPORT, "WSAESOCKTNOSUPPORT", "Socket type not supported."
    },
    {
	WSAESTALE, "WSAESTALE", "Stale NFS file handle."
    },
    {
	WSAETIMEDOUT, "WSAETIMEDOUT", "Connection timed out."
    },
    {
	WSAETOOMANYREFS, "WSAETOOMANYREFS", "Too many references."
    },
    {
	WSAEUSERS, "WSAEUSERS", "Too many users."
    },
    {
	WSAEWOULDBLOCK, "WSAEWOULDBLOCK", "Resource temporarily unavailable."
    },
    {
	WSANOTINITIALISED, "WSANOTINITIALISED", "Successful WSAStartup not yet performed."
    },
    {
	WSASERVICE_NOT_FOUND, "WSASERVICE_NOT_FOUND", "Service not found."
    },
    {
	WSASYSCALLFAILURE, "WSASYSCALLFAILURE", "System call failure."
    },
    {
	WSASYSNOTREADY, "WSASYSNOTREADY", "Network subsystem is unavailable."
    },
    {
	WSATYPE_NOT_FOUND, "WSATYPE_NOT_FOUND", "Class type not found."
    },
    {
	WSAVERNOTSUPPORTED, "WSAVERNOTSUPPORTED", "Winsock.dll version out of range."
    },
    {
	WSAEDISCON, "WSAEDISCON", "Graceful shutdown in progress."
    }
};

/*
 * wsastrerror() - description of WSAGetLastError()
 */
const char *
wsastrerror(int err)
{
    static char xwsaerror_buf[BUFSIZ];
    int i, errind = -1;

    if (err == 0)
	return "(0) No error.";
    for (i = 0; i < sizeof(_wsaerrtext) / sizeof(struct _wsaerrtext); i++) {
	if (_wsaerrtext[i].err != err)
	    continue;
	errind = i;
	break;
    }
    if (errind == -1)
	snprintf(xwsaerror_buf, BUFSIZ, "Unknown");
    else
	snprintf(xwsaerror_buf, BUFSIZ, "%s, %s", _wsaerrtext[errind].errconst, _wsaerrtext[errind].errdesc);
    return xwsaerror_buf;
}

/*
 * WIN32_strerror with argument for late notification */

const char *
WIN32_strerror(int err)
{
    static char xbstrerror_buf[BUFSIZ];

    if (err < 0 || err >= sys_nerr)
	strncpy(xbstrerror_buf, wsastrerror(err), BUFSIZ);
    else
	strncpy(xbstrerror_buf, strerror(err), BUFSIZ);
    return xbstrerror_buf;
}

struct errorentry {
    unsigned long WIN32_code;
    int POSIX_errno;
};

static struct errorentry errortable[] =
{
    {ERROR_INVALID_FUNCTION, EINVAL},
    {ERROR_FILE_NOT_FOUND, ENOENT},
    {ERROR_PATH_NOT_FOUND, ENOENT},
    {ERROR_TOO_MANY_OPEN_FILES, EMFILE},
    {ERROR_ACCESS_DENIED, EACCES},
    {ERROR_INVALID_HANDLE, EBADF},
    {ERROR_ARENA_TRASHED, ENOMEM},
    {ERROR_NOT_ENOUGH_MEMORY, ENOMEM},
    {ERROR_INVALID_BLOCK, ENOMEM},
    {ERROR_BAD_ENVIRONMENT, E2BIG},
    {ERROR_BAD_FORMAT, ENOEXEC},
    {ERROR_INVALID_ACCESS, EINVAL},
    {ERROR_INVALID_DATA, EINVAL},
    {ERROR_INVALID_DRIVE, ENOENT},
    {ERROR_CURRENT_DIRECTORY, EACCES},
    {ERROR_NOT_SAME_DEVICE, EXDEV},
    {ERROR_NO_MORE_FILES, ENOENT},
    {ERROR_LOCK_VIOLATION, EACCES},
    {ERROR_BAD_NETPATH, ENOENT},
    {ERROR_NETWORK_ACCESS_DENIED, EACCES},
    {ERROR_BAD_NET_NAME, ENOENT},
    {ERROR_FILE_EXISTS, EEXIST},
    {ERROR_CANNOT_MAKE, EACCES},
    {ERROR_FAIL_I24, EACCES},
    {ERROR_INVALID_PARAMETER, EINVAL},
    {ERROR_NO_PROC_SLOTS, EAGAIN},
    {ERROR_DRIVE_LOCKED, EACCES},
    {ERROR_BROKEN_PIPE, EPIPE},
    {ERROR_DISK_FULL, ENOSPC},
    {ERROR_INVALID_TARGET_HANDLE, EBADF},
    {ERROR_INVALID_HANDLE, EINVAL},
    {ERROR_WAIT_NO_CHILDREN, ECHILD},
    {ERROR_CHILD_NOT_COMPLETE, ECHILD},
    {ERROR_DIRECT_ACCESS_HANDLE, EBADF},
    {ERROR_NEGATIVE_SEEK, EINVAL},
    {ERROR_SEEK_ON_DEVICE, EACCES},
    {ERROR_DIR_NOT_EMPTY, ENOTEMPTY},
    {ERROR_NOT_LOCKED, EACCES},
    {ERROR_BAD_PATHNAME, ENOENT},
    {ERROR_MAX_THRDS_REACHED, EAGAIN},
    {ERROR_LOCK_FAILED, EACCES},
    {ERROR_ALREADY_EXISTS, EEXIST},
    {ERROR_FILENAME_EXCED_RANGE, ENOENT},
    {ERROR_NESTING_NOT_ALLOWED, EAGAIN},
    {ERROR_NOT_ENOUGH_QUOTA, ENOMEM}
};

#define MIN_EXEC_ERROR ERROR_INVALID_STARTING_CODESEG
#define MAX_EXEC_ERROR ERROR_INFLOOP_IN_RELOC_CHAIN

#define MIN_EACCES_RANGE ERROR_WRITE_PROTECT
#define MAX_EACCES_RANGE ERROR_SHARING_BUFFER_EXCEEDED

void
WIN32_maperror(unsigned long WIN32_oserrno)
{
    int i;

    _doserrno = WIN32_oserrno;
    for (i = 0; i < (sizeof(errortable) / sizeof(struct errorentry)); ++i) {
	if (WIN32_oserrno == errortable[i].WIN32_code) {
	    errno = errortable[i].POSIX_errno;
	    return;
	}
    }
    if (WIN32_oserrno >= MIN_EACCES_RANGE && WIN32_oserrno <= MAX_EACCES_RANGE)
	errno = EACCES;
    else if (WIN32_oserrno >= MIN_EXEC_ERROR && WIN32_oserrno <= MAX_EXEC_ERROR)
	errno = ENOEXEC;
    else
	errno = EINVAL;
}

#endif /* _SQUID_MSWIN_ */
