
/*
 * $Id: win32lib.c 14577 2010-04-11 02:51:02Z adrian.chadd $
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
#include <sys/fcntl.h>
#if HAVE_WIN32_PSAPI
#include <psapi.h>
#endif

#include "win32_compat.h"
#include "win32_version.h"

THREADLOCAL int ws32_result;
THREADLOCAL int _so_err;
THREADLOCAL int _so_err_siz = sizeof(int);
LPCRITICAL_SECTION dbg_mutex = NULL;

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


#if defined(_MSC_VER)		/* Microsoft C Compiler ONLY */
size_t
getpagesize()
{
    static DWORD system_pagesize = 0;
    if (!system_pagesize) {
	SYSTEM_INFO system_info;
	GetSystemInfo(&system_info);
	system_pagesize = system_info.dwPageSize;
    }
    return system_pagesize;
}

int64_t
WIN32_strtoll(const char *nptr, char **endptr, int base)
{
    const char *s;
    int64_t acc;
    int64_t val;
    int neg, any;
    char c;

    /*
     * Skip white space and pick up leading +/- sign if any.
     * If base is 0, allow 0x for hex and 0 for octal, else
     * assume decimal; if base is already 16, allow 0x.
     */
    s = nptr;
    do {
	c = *s++;
    } while (xisspace(c));
    if (c == '-') {
	neg = 1;
	c = *s++;
    } else {
	neg = 0;
	if (c == '+')
	    c = *s++;
    }
    if ((base == 0 || base == 16) &&
	c == '0' && (*s == 'x' || *s == 'X')) {
	c = s[1];
	s += 2;
	base = 16;
    }
    if (base == 0)
	base = c == '0' ? 8 : 10;
    acc = any = 0;
    if (base < 2 || base > 36) {
	errno = EINVAL;
	if (endptr != NULL)
	    *endptr = (char *) (any ? s - 1 : nptr);
	return acc;
    }
    /* The classic bsd implementation requires div/mod operators
     * to compute a cutoff.  Benchmarking proves that is very, very
     * evil to some 32 bit processors.  Instead, look for underflow
     * in both the mult and add/sub operation.  Unlike the bsd impl,
     * we also work strictly in a signed int64 word as we haven't
     * implemented the unsigned type in win32.
     * 
     * Set 'any' if any `digits' consumed; make it negative to indicate
     * overflow.
     */
    val = 0;
    for (;; c = *s++) {
	if (c >= '0' && c <= '9')
	    c -= '0';
	else if (c >= 'A' && c <= 'Z')
	    c -= 'A' - 10;
	else if (c >= 'a' && c <= 'z')
	    c -= 'a' - 10;
	else
	    break;
	if (c >= base)
	    break;
	val *= base;
	if ((any < 0)		/* already noted an over/under flow - short circuit */
	    ||(neg && (val > acc || (val -= c) > acc))	/* underflow */
	    ||(!neg && (val < acc || (val += c) < acc))) {	/* overflow */
	    any = -1;		/* once noted, over/underflows never go away */
	} else {
	    acc = val;
	    any = 1;
	}
    }

    if (any < 0) {
	acc = neg ? INT64_MIN : INT64_MAX;
	errno = ERANGE;
    } else if (!any) {
	errno = EINVAL;
    }
    if (endptr != NULL)
	*endptr = (char *) (any ? s - 1 : nptr);
    return (acc);
}
#endif

uid_t
geteuid(void)
{
    return 100;
}

uid_t
getuid(void)
{
    return 100;
}

int
setuid(uid_t uid)
{
    return 0;
}

int
seteuid(uid_t euid)
{
    return 0;
}

gid_t
getegid(void)
{
    return 100;
}

gid_t
getgid(void)
{
    return 100;
}

int
setgid(gid_t gid)
{
    return 0;
}

int
setegid(gid_t egid)
{
    return 0;
}

int
chroot(const char *dirname)
{
    if (SetCurrentDirectory(dirname))
	return 0;
    else
	return GetLastError();
}

/* Convert from "a.b.c.d" IP address string into
 * an in_addr structure.  Returns 0 on failure,
 * and 1 on success.
 */
int
inet_aton(const char *cp, struct in_addr *addr)
{
    if (cp == NULL || addr == NULL) {
	return (0);
    }
    addr->s_addr = inet_addr(cp);
    return (addr->s_addr == INADDR_NONE) ? 0 : 1;
}

void
GetProcessName(pid_t pid, char *ProcessName)
{
    HANDLE hProcess;

    strcpy(ProcessName, "unknown");
#if HAVE_WIN32_PSAPI
    /* Get a handle to the process. */
    hProcess = OpenProcess(PROCESS_QUERY_INFORMATION |
	PROCESS_VM_READ,
	FALSE, pid);
    /* Get the process name. */
    if (NULL != hProcess) {
	HMODULE hMod;
	DWORD cbNeeded;

	if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded))
	    GetModuleBaseName(hProcess, hMod, ProcessName, sizeof(ProcessName));
	else {
	    CloseHandle(hProcess);
	    return;
	}
    } else
	return;
    CloseHandle(hProcess);
#endif
}

int
kill(pid_t pid, int sig)
{
    HANDLE hProcess;
    char MyProcessName[MAX_PATH];
    char ProcessNameToCheck[MAX_PATH];

    if (sig == 0) {
	if ((hProcess = OpenProcess(PROCESS_QUERY_INFORMATION |
		    PROCESS_VM_READ,
		    FALSE, pid)) == NULL)
	    return -1;
	else {
	    CloseHandle(hProcess);
	    GetProcessName(getpid(), MyProcessName);
	    GetProcessName(pid, ProcessNameToCheck);
	    if (strcmp(MyProcessName, ProcessNameToCheck) == 0)
		return 0;
	    return -1;
	}
    } else
	return 0;
}

#ifndef HAVE_GETTIMEOFDAY
int
gettimeofday(struct timeval *pcur_time, struct timezone *tz)
{

    struct _timeb current;

    _ftime(&current);

    pcur_time->tv_sec = current.time;
    pcur_time->tv_usec = current.millitm * 1000L;
    if (tz) {
	tz->tz_minuteswest = current.timezone;	/* minutes west of Greenwich  */
	tz->tz_dsttime = current.dstflag;	/* type of dst correction  */
    }
    return 0;
}
#endif

int
statfs(const char *path, struct statfs *sfs)
{
    char drive[4];
    DWORD spc, bps, freec, totalc;
    DWORD vsn, maxlen, flags;

    if (!sfs) {
	errno = EINVAL;
	return -1;
    }
    strncpy(drive, path, 2);
    drive[2] = '\0';
    strcat(drive, "\\");

    if (!GetDiskFreeSpace(drive, &spc, &bps, &freec, &totalc)) {
	errno = ENOENT;
	return -1;
    }
    if (!GetVolumeInformation(drive, NULL, 0, &vsn, &maxlen, &flags, NULL, 0)) {
	errno = ENOENT;
	return -1;
    }
    sfs->f_type = flags;
    sfs->f_bsize = spc * bps;
    sfs->f_blocks = totalc;
    sfs->f_bfree = sfs->f_bavail = freec;
    sfs->f_files = -1;
    sfs->f_ffree = -1;
    sfs->f_fsid = vsn;
    sfs->f_namelen = maxlen;
    return 0;
}

int
WIN32_ftruncate(int fd, off_t size)
{
    HANDLE file;
    DWORD error;
    LARGE_INTEGER size64;
    LARGE_INTEGER test64;

    if (fd < 0) {
	errno = EBADF;
	return -1;
    }
    size64.QuadPart = (__int64) size;
    test64.QuadPart = 0;

    file = (HANDLE) _get_osfhandle(fd);

    /* Get current file position to check File Handle */
    test64.LowPart = SetFilePointer(file, test64.LowPart, &test64.HighPart, FILE_CURRENT);
    if ((test64.LowPart == INVALID_SET_FILE_POINTER) && ((error = GetLastError()) != NO_ERROR))
	goto WIN32_ftruncate_error;

    /* Set the current File Pointer position */
    size64.LowPart = SetFilePointer(file, size64.LowPart, &size64.HighPart, FILE_BEGIN);
    if ((size64.LowPart == INVALID_SET_FILE_POINTER) && ((error = GetLastError()) != NO_ERROR))
	goto WIN32_ftruncate_error;
    else if (!SetEndOfFile(file)) {
	error = GetLastError();
	goto WIN32_ftruncate_error;
    }
    return 0;

  WIN32_ftruncate_error:
    switch (error) {
    case ERROR_INVALID_HANDLE:
	errno = EBADF;
	break;
    default:
	errno = EIO;
	break;
    }

    return -1;
}

int
WIN32_truncate(const char *pathname, off_t length)
{
    int fd;
    int res = -1;

    fd = open(pathname, O_RDWR);

    if (fd == -1)
	errno = EBADF;
    else {
	res = WIN32_ftruncate(fd, length);
	_close(fd);
    }

    return res;
}

struct passwd *
getpwnam(char *unused)
{
    static struct passwd pwd =
    {NULL, NULL, 100, 100, NULL, NULL, NULL};
    return &pwd;
}

struct group *
getgrnam(char *unused)
{
    static struct group grp =
    {NULL, NULL, 100, NULL};
    return &grp;
}

int
WIN32_Close_FD_Socket(int fd)
{
    int result = 0;

    if (closesocket(_get_osfhandle(fd)) == SOCKET_ERROR) {
	errno = WSAGetLastError();
	result = 1;
    }
    _free_osfhnd(fd);
    _osfile(fd) = 0;
    return result;
}

#if defined(__MINGW32__)	/* MinGW environment */
int
_free_osfhnd(int filehandle)
{
    if (((unsigned) filehandle < SQUID_MAXFD) &&
	(_osfile(filehandle) & FOPEN) &&
	(_osfhnd(filehandle) != (long) INVALID_HANDLE_VALUE)) {
	switch (filehandle) {
	case 0:
	    SetStdHandle(STD_INPUT_HANDLE, NULL);
	    break;
	case 1:
	    SetStdHandle(STD_OUTPUT_HANDLE, NULL);
	    break;
	case 2:
	    SetStdHandle(STD_ERROR_HANDLE, NULL);
	    break;
	}
	_osfhnd(filehandle) = (long) INVALID_HANDLE_VALUE;
	return (0);
    } else {
	errno = EBADF;		/* bad handle */
	_doserrno = 0L;		/* not an OS error */
	return -1;
    }
}
#endif

#if defined(_SQUID_MSWIN_)

void
setbuffer(FILE *stream, char *buf, size_t size)
{
	(void) setvbuf(stream, buf, buf ? _IOFBF : _IONBF, size);
}

int
WIN32_getrusage(int who, struct rusage *usage)
{
#if HAVE_WIN32_PSAPI
    if ((WIN32_OS_version == _WIN_OS_WINNT) || (WIN32_OS_version == _WIN_OS_WIN2K)
	|| (WIN32_OS_version == _WIN_OS_WINXP) || (WIN32_OS_version == _WIN_OS_WINNET)) {
	/* On Windows NT/2000 call PSAPI.DLL for process Memory */
	/* informations -- Guido Serassio                       */
	HANDLE hProcess;
	PROCESS_MEMORY_COUNTERS pmc;
	hProcess = OpenProcess(PROCESS_QUERY_INFORMATION |
	    PROCESS_VM_READ,
	    FALSE, GetCurrentProcessId());
	{
	    /* Microsoft CRT doesn't have getrusage function,  */
	    /* so we get process CPU time information from PSAPI.DLL. */
	    FILETIME ftCreate, ftExit, ftKernel, ftUser;
	    if (GetProcessTimes(hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
		PFILETIME p_ftUser = &ftUser;
		PFILETIME p_ftKernel = &ftKernel;
		int64_t tUser64 = (*(int64_t *) p_ftUser / 10);
		int64_t tKernel64 = (*(int64_t *) p_ftKernel / 10);
		usage->ru_utime.tv_sec = (long) (tUser64 / 1000000);
		usage->ru_stime.tv_sec = (long) (tKernel64 / 1000000);
		usage->ru_utime.tv_usec = (long) (tUser64 % 1000000);
		usage->ru_stime.tv_usec = (long) (tKernel64 % 1000000);
	    } else {
		CloseHandle(hProcess);
		return -1;
	    }
	}
	if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
	    usage->ru_maxrss = (DWORD) (pmc.WorkingSetSize / getpagesize());
	    usage->ru_majflt = pmc.PageFaultCount;
	} else {
	    CloseHandle(hProcess);
	    return -1;
	}
	CloseHandle(hProcess);
    }
#endif
    return 0;
}

#endif

#endif /* _SQUID_MSWIN_ */

