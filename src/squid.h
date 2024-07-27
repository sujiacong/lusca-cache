
/*
 * $Id: squid.h 14694 2010-05-23 13:06:44Z adrian.chadd $
 *
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

#ifndef SQUID_H
#define SQUID_H

#include "config.h"

/*
 * On some systems, FD_SETSIZE is set to something lower than the
 * actual number of files which can be opened.  IRIX is one case,
 * NetBSD is another.  So here we increase FD_SETSIZE to our
 * configure-discovered maximum *before* any system includes.
 */
#define CHANGE_FD_SETSIZE 1

/*
 * Cannot increase FD_SETSIZE on Linux, but we can increase __FD_SETSIZE
 * with glibc 2.2 (or later? remains to be seen). We do this by including
 * bits/types.h which defines __FD_SETSIZE first, then we redefine
 * __FD_SETSIZE. Ofcourse a user program may NEVER include bits/whatever.h
 * directly, so this is a dirty hack!
 */
#if defined(_SQUID_LINUX_)
#undef CHANGE_FD_SETSIZE
#define CHANGE_FD_SETSIZE 0
#include <features.h>
#if (__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 2)
#if SQUID_MAXFD > DEFAULT_FD_SETSIZE
#include <bits/types.h>
#undef __FD_SETSIZE
#define __FD_SETSIZE SQUID_MAXFD
#endif
#endif
#endif

/*
 * Cannot increase FD_SETSIZE on FreeBSD before 2.2.0, causes select(2)
 * to return EINVAL.
 * --Marian Durkovic <marian@svf.stuba.sk>
 * --Peter Wemm <peter@spinner.DIALix.COM>
 */
#if defined(_SQUID_FREEBSD_)
#include <osreldate.h>
#if __FreeBSD_version < 220000
#undef CHANGE_FD_SETSIZE
#define CHANGE_FD_SETSIZE 0
#endif
#endif

/*
 * Trying to redefine CHANGE_FD_SETSIZE causes a slew of warnings
 * on Mac OS X Server.
 */
#if defined(_SQUID_APPLE_)
#undef CHANGE_FD_SETSIZE
#define CHANGE_FD_SETSIZE 0
#endif

/* Increase FD_SETSIZE if SQUID_MAXFD is bigger */
#if CHANGE_FD_SETSIZE && SQUID_MAXFD > DEFAULT_FD_SETSIZE
#undef FD_SETSIZE
#define FD_SETSIZE SQUID_MAXFD
#endif

#if PURIFY
#define LEAK_CHECK_MODE 1
#elif WITH_VALGRIND
#define LEAK_CHECK_MODE 1
#elif XMALLOC_TRACE
#define LEAK_CHECK_MODE 1
#endif

#if defined(NODEBUG)
#define assert(EX) ((void)0)
#elif STDC_HEADERS
#define assert(EX)  ((EX)?((void)0):xassert( # EX , __FILE__, __LINE__))
#else
#define assert(EX)  ((EX)?((void)0):xassert("EX", __FILE__, __LINE__))
#endif


/* 32 bit integer compatability */
#include "squid_types.h"
#define num32 int32_t
#define u_num32 u_int32_t

#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_STDIO_H
#include <stdio.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_CTYPE_H
#include <ctype.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif
#if HAVE_GRP_H
#include <grp.h>
#endif
#if HAVE_GNUMALLOC_H
#include <gnumalloc.h>
#elif HAVE_MALLOC_H
#include <malloc.h>
#endif
#if HAVE_MEMORY_H
#include <memory.h>
#endif
#if HAVE_NETDB_H && !defined(_SQUID_NETDB_H_)	/* protect NEXTSTEP */
#define _SQUID_NETDB_H_
#ifdef _SQUID_NEXT_
#include <netinet/in_systm.h>
#endif
#include <netdb.h>
#endif
#if HAVE_PATHS_H
#include <paths.h>
#endif
#if HAVE_PWD_H
#include <pwd.h>
#endif
#if HAVE_SIGNAL_H
#include <signal.h>
#endif
#if HAVE_TIME_H
#include <time.h>
#endif
#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#if HAVE_SYS_RESOURCE_H
#include <sys/resource.h>	/* needs sys/time.h above it */
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
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#if HAVE_LIBC_H
#include <libc.h>
#endif
#ifdef HAVE_SYS_SYSCALL_H
#include <sys/syscall.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#if HAVE_BSTRING_H
#include <bstring.h>
#endif
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#if HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef _SQUID_WIN32_
#include <io.h>
#endif

#if HAVE_DIRENT_H
#include <dirent.h>
#define NAMLEN(dirent) strlen((dirent)->d_name)
#else /* HAVE_DIRENT_H */
#define dirent direct
#define NAMLEN(dirent) (dirent)->d_namlen
#if HAVE_SYS_NDIR_H
#include <sys/ndir.h>
#endif /* HAVE_SYS_NDIR_H */
#if HAVE_SYS_DIR_H
#include <sys/dir.h>
#endif /* HAVE_SYS_DIR_H */
#if HAVE_NDIR_H
#include <ndir.h>
#endif /* HAVE_NDIR_H */
#endif /* HAVE_DIRENT_H */

#if defined(__QNX__)
#include <unix.h>
#endif

#if HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif

#include "../libcore/varargs.h"

/* Make sure syslog goes after stdarg/varargs */
#ifdef HAVE_SYSLOG_H
#ifdef _SQUID_AIX_
#define _XOPEN_EXTENDED_SOURCE
#define _XOPEN_SOURCE_EXTENDED 1
#endif
#include <syslog.h>
#endif

#include "../libcore/syslog_ntoa.h"

#if HAVE_MATH_H
#include <math.h>
#endif

/* This is for SQUIDHOSTNAMELEN */
#include "../libsqurl/defines.h"

#if !HAVE_GETRUSAGE
#if defined(_SQUID_HPUX_)
#define HAVE_GETRUSAGE 1
#define getrusage(a, b)  syscall(SYS_GETRUSAGE, a, b)
#endif
#endif

#if !HAVE_STRUCT_RUSAGE
/*
 * If we don't have getrusage() then we create a fake structure
 * with only the fields Squid cares about.  This just makes the
 * source code cleaner, so we don't need lots of #ifdefs in other
 * places
 */
struct rusage {
    struct timeval ru_stime;
    struct timeval ru_utime;
    int ru_maxrss;
    int ru_majflt;
};

#endif

#if !defined(HAVE_GETPAGESIZE) && defined(_SQUID_HPUX_)
#define HAVE_GETPAGESIZE
#define getpagesize( )   sysconf(_SC_PAGE_SIZE)
#endif

#ifndef BUFSIZ
#define BUFSIZ  4096		/* make reasonable guess */
#endif


#ifndef SA_RESTART
#define SA_RESTART 0
#endif
#ifndef SA_NODEFER
#define SA_NODEFER 0
#endif
#ifndef SA_RESETHAND
#define SA_RESETHAND 0
#endif
#if SA_RESETHAND == 0 && defined(SA_ONESHOT)
#undef SA_RESETHAND
#define SA_RESETHAND SA_ONESHOT
#endif

#if USE_LEAKFINDER
#define leakAdd(p) leakAddFL(p,__FILE__,__LINE__)
#define leakTouch(p) leakTouchFL(p,__FILE__,__LINE__)
#define leakFree(p) leakFreeFL(p,__FILE__,__LINE__)
#else
#define leakAdd(p) p
#define leakTouch(p) p
#define leakFree(p) p
#endif

#if defined(_SQUID_NEXT_) && !defined(S_ISDIR)
#define S_ISDIR(mode) (((mode) & (_S_IFMT)) == (_S_IFDIR))
#endif

#ifdef USE_GNUREGEX
#include "GNUregex.h"
#elif HAVE_REGEX_H
#include <regex.h>
#endif

#include "squid_md5.h"

#include "Stack.h"
#include "../include/Vector.h"

/* Needed for poll() on Linux at least */
#if USE_POLL
#ifndef POLLRDNORM
#define POLLRDNORM POLLIN
#endif
#ifndef POLLWRNORM
#define POLLWRNORM POLLOUT
#endif
#endif

#ifdef SQUID_SNMP
#include "cache_snmp.h"
#endif

#include "hash.h"
#include "rfc1035.h"

/* Windows Port */
#ifdef _SQUID_WIN32_ 
#include "../include/win32_compat.h"
#include "../include/win32_version.h"
#include "../include/win32_error.h"
#endif

#include "../libcore/dlink.h"
#include "../libcore/fifo.h"
#include "../libcore/tools.h"
#include "../libcore/kb.h"
#include "../libcore/gb.h"

#include "../libsqdebug/ctx.h"
#include "../libsqdebug/debug.h"
#include "../libsqdebug/debug_syslog.h"
#include "../libsqdebug/debug_file.h"

#include "../libmem/MemPool.h"
#include "../libmem/MemStr.h"
#include "../libmem/buf.h"
#include "../libmem/String.h"
#include "../libmem/StrList.h"
#include "../libmem/wordlist.h"
#include "../libmem/intlist.h"
#include "../libmem/MemBufs.h"
#include "../libmem/MemBuf.h"

#include "../libcb/cbdata.h"

#include "../libstat/StatHist.h"

#include "../libsqinet/inet_legacy.h"
#include "../libsqinet/sqinet.h"
#include "../libsqinet/inet_legacy.h"

#include "../libsqident/ident.h"

#include "../libmime/MimeHdrs.h"

#include "../libhttp/HttpVersion.h"
#include "../libhttp/HttpStatusLine.h"
#include "../libhttp/HttpHeaderType.h"
#include "../libhttp/HttpHeaderFieldStat.h"
#include "../libhttp/HttpHeaderFieldInfo.h"
#include "../libhttp/HttpHeaderEntry.h"
#include "../libhttp/HttpHeader.h"
#include "../libhttp/HttpHeaderStats.h"
#include "../libhttp/HttpHeaderVars.h"
#include "../libhttp/HttpHeaderTools.h"
#include "../libhttp/HttpHeaderMask.h"
#include "../libhttp/HttpHeaderList.h"
#include "../libhttp/HttpHeaderParse.h"
#include "../libhttp/HttpHeaderGet.h"
#include "../libhttp/HttpHeaderPut.h"
#include "../libhttp/HttpHdrCc.h"
#include "../libhttp/HttpMsg.h"
#include "../libhttp/HttpHdrRange.h"
#include "../libhttp/HttpHdrContRange.h"
#include "../libhttp/HttpBody.h"
#include "../libhttp/HttpReply.h"
#include "../libhttp/HttpMethod.h"

#include "../libiapp/event.h"
#include "../libiapp/iapp_ssl.h"
#include "../libiapp/signals.h"
#include "../libiapp/iapp_ssl.h"
#include "../libiapp/ssl_support.h"
#include "../libiapp/fd_types.h"
#include "../libiapp/comm_types.h"
#include "../libiapp/comm.h"
#include "../libiapp/disk.h"
#include "../libiapp/comm_ips.h"
#include "../libiapp/globals.h"
#include "../libiapp/pconn_hist.h"
#include "../libiapp/mainloop.h"

#include "../libhelper/ipc.h"
#include "../libhelper/helper.h"

#include "../libsqident/ident.h"

#include "../libsqdns/dns.h"

#include "../libsqname/namecfg.h"
#include "../libsqname/ipcache.h"
#include "../libsqname/fqdncache.h"

#include "../libsqdns/dns_internal.h"

#include "../libstmem/stmem.h"

#include "../libsqtlv/tlv.h"

#include "../libsqstore/store_key.h"
#include "../libsqstore/store_mgr.h"
#include "../libsqstore/store_meta.h"
#include "../libsqstore/store_log.h"
#include "../libsqstore/store_file_ufs.h"

#include "../libsqurl/proto.h"

#include "../libmutiprocess/multiprocess.h"

#include "defines.h"
#include "enums.h"
#include "typedefs.h"
#include "structs.h"
#include "protos.h"
#include "globals.h"

#include "util.h"

#if !HAVE_TEMPNAM
#include "tempnam.h"
#endif

#if !HAVE_INITGROUPS
#include "initgroups.h"
#endif

/*
 * Squid source files should not call these functions directly.
 * Use xmalloc, xfree, xcalloc, snprintf, and xstrdup instead.
 * Also use xmemcpy, xisspace, ...
 */
#ifndef malloc
#define malloc +
#endif
#ifndef free
#define free +
#endif
#ifndef calloc
#define calloc +
#endif
#ifndef sprintf
#define sprintf +
#endif
#ifndef strdup
#define strdup +
#endif

/*
 * I'm sick of having to keep doing this ..
 */
#define INDEXSD(i)   (&Config.cacheSwap.swapDirs[(i)])

#ifndef IPPROTO_UDP
#define IPPROTO_UDP 0
#endif

#ifndef IPPROTO_TCP
#define IPPROTO_TCP 0
#endif


#if defined(_SQUID_MSWIN_)
/* Windows lacks getpagesize() prototype */
#ifndef getpagesize
extern size_t getpagesize(void);
#endif
#if defined(_MSC_VER)		/* Microsoft C Compiler ONLY */
#define strtoll WIN32_strtoll
#endif
#endif /* _SQUID_MSWIN_ */

/*
 * Trap attempts to build large file cache support without support for
 * large objects
 */
#if LARGE_CACHE_FILES && SIZEOF_SQUID_OFF_T <= 4
#error Your platform does not support large integers. Can not build with --enable-large-cache-files
#endif

/*
 * valgrind debug support
 */
#include "../libcore/valgrind.h"

/* For now - these need to move! [ahc] */
extern MemPool *acl_name_list_pool;
extern MemPool *acl_deny_pool;
#if USE_CACHE_DIGESTS
extern MemPool *pool_cache_digest;
#endif
extern MemPool *pool_fwd_server;
extern MemPool * pool_http_reply;
extern MemPool * pool_http_header_entry;
extern MemPool * pool_http_hdr_range_spec;
extern MemPool * pool_http_hdr_range;
extern MemPool * pool_http_hdr_cont_range;
extern MemPool * pool_mem_node;
extern MemPool * pool_storeentry;
extern MemPool * pool_memobject;
extern MemPool * pool_swap_tlv;
extern MemPool * pool_swap_log_data;

extern int DebugSignal; 

CBDATA_GLOBAL_TYPE(RemovalPolicy);
CBDATA_GLOBAL_TYPE(RemovalPolicyWalker);
CBDATA_GLOBAL_TYPE(RemovalPurgeWalker);
CBDATA_GLOBAL_TYPE(ps_state);
CBDATA_GLOBAL_TYPE(storeIOState);

/* src/MemBuf.c */
extern int buf_read(buf_t *b, int fd, int grow_size);
extern int memBufFill(MemBuf *mb, int fd, int grow_size);

#endif /* SQUID_H */
