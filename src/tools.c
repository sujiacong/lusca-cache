
/*
 * $Id: tools.c 14416 2010-02-17 06:39:36Z adrian.chadd $
 *
 * DEBUG: section 21    Misc Functions
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
 * tools.c for the correct handling of FD<=>socket magic
 */
#define TOOLS_C

#include "squid.h"

#ifdef _SQUID_LINUX_
#if HAVE_SYS_CAPABILITY_H
#include <sys/capability.h>
#endif
#endif

#if HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#define DEAD_MSG "\
The Squid Cache (version %s) died.\n\
\n\
You've encountered a fatal error in the Squid Cache version %s.\n\
If a core file was created (possibly in the swap directory),\n\
please execute 'gdb squid core' or 'dbx squid core', then type 'where',\n\
and report the trace back to squid-bugs@squid-cache.org.\n\
\n\
Thanks!\n"

int DebugSignal = -1;
static void fatal_common(const char *);
static void mail_warranty(void);
#if MEM_GEN_TRACE
extern void log_trace_done();
extern void log_trace_init(char *);
#endif
static void restoreCapabilities(int keep);

#ifdef _SQUID_LINUX_
/* Workaround for crappy glic header files */
extern int backtrace(void *, int);
extern void backtrace_symbols_fd(void *, int, int);
extern int setresuid(uid_t, uid_t, uid_t);
#endif /* _SQUID_LINUX */

extern void (*failure_notify) (const char *);

MemPool *dlink_node_pool = NULL;

void
releaseServerSockets(void)
{
    int i;
    /* Release the main ports as early as possible */
    for (i = 0; i < NHttpSockets; i++) {
	if (HttpSockets[i] >= 0)
	    close(HttpSockets[i]);
    }
    if (theInIcpConnection >= 0)
	close(theInIcpConnection);
    if (theOutIcpConnection >= 0 && theOutIcpConnection != theInIcpConnection)
	close(theOutIcpConnection);
}

static char *
dead_msg(void)
{
    LOCAL_ARRAY(char, msg, 1024);
    snprintf(msg, 1024, DEAD_MSG, version_string, version_string);
    return msg;
}

static void
mail_warranty(void)
{
    FILE *fp = NULL;
    static char command[256];
#if HAVE_MKSTEMP
    char filename[] = "/tmp/squid-XXXXXX";
    int tfd = mkstemp(filename);
    if (tfd < 0)
	return;
    if ((fp = fdopen(tfd, "w")) == NULL)
	return;
#else
    char *filename;
    if ((filename = tempnam(NULL, appname)) == NULL)
	return;
    if ((fp = fopen(filename, "w")) == NULL)
	return;
#endif
    if (Config.EmailFrom)
	fprintf(fp, "From: %s\n", Config.EmailFrom);
    else
	fprintf(fp, "From: %s@%s\n", appname, uniqueHostname());
    fprintf(fp, "To: %s\n", Config.adminEmail);
    fprintf(fp, "Subject: %s\n", dead_msg());
    fclose(fp);
    snprintf(command, 256, "%s %s < %s", Config.EmailProgram, Config.adminEmail, filename);
    system(command);		/* XXX should avoid system(3) */
    unlink(filename);
}

void
dumpMallocStats(void)
{
#if HAVE_MSTATS && HAVE_GNUMALLOC_H
    struct mstats ms = mstats();
    fprintf(debug_log, "\ttotal space in arena:  %6d KB\n",
	(int) (ms.bytes_total >> 10));
    fprintf(debug_log, "\tTotal free:            %6d KB %d%%\n",
	(int) (ms.bytes_free >> 10),
	percent(ms.bytes_free, ms.bytes_total));
#elif HAVE_MALLINFO && HAVE_STRUCT_MALLINFO
    struct mallinfo mp;
    int t;
    if (!do_mallinfo)
	return;
    mp = mallinfo();
    fprintf(debug_log, "Memory usage for %s via mallinfo():\n", appname);
    fprintf(debug_log, "\ttotal space in arena:  %6ld KB\n",
	(long int) (mp.arena >> 10));
    fprintf(debug_log, "\tOrdinary blocks:       %6ld KB %6ld blks\n",
	(long int) (mp.uordblks >> 10), (long int) mp.ordblks);
    fprintf(debug_log, "\tSmall blocks:          %6ld KB %6ld blks\n",
	(long int) (mp.usmblks >> 10), (long int) mp.smblks);
    fprintf(debug_log, "\tHolding blocks:        %6ld KB %6ld blks\n",
	(long int) (mp.hblkhd >> 10), (long int) mp.hblks);
    fprintf(debug_log, "\tFree Small blocks:     %6ld KB\n",
	(long int) (mp.fsmblks >> 10));
    fprintf(debug_log, "\tFree Ordinary blocks:  %6ld KB\n",
	(long int) (mp.fordblks >> 10));
    t = (mp.uordblks + mp.usmblks + mp.hblkhd) >> 10;
    fprintf(debug_log, "\tTotal in use:          %6d KB %d%%\n",
	t, percent(t, (mp.arena + mp.hblkhd) >> 10));
    t = (mp.fsmblks + mp.fordblks) >> 10;
    fprintf(debug_log, "\tTotal free:            %6d KB %d%%\n",
	t, percent(t, (mp.arena + mp.hblkhd) >> 10));
#if HAVE_EXT_MALLINFO
    fprintf(debug_log, "\tmax size of small blocks:\t%d\n",
	mp.mxfast);
    fprintf(debug_log, "\tnumber of small blocks in a holding block:\t%d\n",
	mp.nlblks);
    fprintf(debug_log, "\tsmall block rounding factor:\t%d\n",
	mp.grain);
    fprintf(debug_log, "\tspace (including overhead) allocated in ord. blks:\t%d\n",
	mp.uordbytes);
    fprintf(debug_log, "\tnumber of ordinary blocks allocated:\t%d\n",
	mp.allocated);
    fprintf(debug_log, "\tbytes used in maintaining the free tree:\t%d\n",
	mp.treeoverhead);
#endif /* HAVE_EXT_MALLINFO */
#endif /* HAVE_MALLINFO */
}

void
squid_getrusage(struct rusage *r)
{
    memset(r, '\0', sizeof(struct rusage));
#if HAVE_GETRUSAGE && defined(RUSAGE_SELF)
#ifdef _SQUID_SOLARIS_
    /* Solaris 2.5 has getrusage() permission bug -- Arjan de Vet */
    enter_suid();
#endif
    getrusage(RUSAGE_SELF, r);
#ifdef _SQUID_SOLARIS_
    leave_suid();
#endif
#endif
}

double
rusage_cputime(struct rusage *r)
{
    return (double) r->ru_stime.tv_sec +
	(double) r->ru_utime.tv_sec +
	(double) r->ru_stime.tv_usec / 1000000.0 +
	(double) r->ru_utime.tv_usec / 1000000.0;
}

/* Hack for some HP-UX preprocessors */
#ifndef HAVE_GETPAGESIZE
#define HAVE_GETPAGESIZE 0
#endif

int
rusage_maxrss(struct rusage *r)
{
#if defined(_SQUID_SGI_) && _ABIAPI
    return r->ru_pad[0];
#elif defined(_SQUID_SGI_)
    return r->ru_maxrss;
#elif defined(_SQUID_OSF_)
    return r->ru_maxrss;
#elif defined(_SQUID_AIX_)
    return r->ru_maxrss;
#elif defined(BSD4_4)
    return r->ru_maxrss;
#elif defined(HAVE_GETPAGESIZE) && HAVE_GETPAGESIZE != 0
    return (r->ru_maxrss * getpagesize()) >> 10;
#elif defined(PAGESIZE)
    return (r->ru_maxrss * PAGESIZE) >> 10;
#else
    return r->ru_maxrss;
#endif
}

int
rusage_pagefaults(struct rusage *r)
{
#if defined(_SQUID_SGI_) && _ABIAPI
    return r->ru_pad[5];
#else
    return r->ru_majflt;
#endif
}


void
PrintRusage(void)
{
    struct rusage rusage;
    squid_getrusage(&rusage);
    fprintf(debug_log, "CPU Usage: %.3f seconds = %.3f user + %.3f sys\n",
	rusage_cputime(&rusage),
	rusage.ru_utime.tv_sec + ((double) rusage.ru_utime.tv_usec / 1000000.0),
	rusage.ru_stime.tv_sec + ((double) rusage.ru_stime.tv_usec / 1000000.0));
    fprintf(debug_log, "Maximum Resident Size: %d KB\n",
	rusage_maxrss(&rusage));
    fprintf(debug_log, "Page faults with physical i/o: %d\n",
	rusage_pagefaults(&rusage));
}


void
death(int sig)
{
    if (sig == SIGSEGV)
	fprintf(debug_log, "FATAL: Received Segment Violation...dying.\n");
    else if (sig == SIGBUS)
	fprintf(debug_log, "FATAL: Received Bus Error...dying.\n");
    else
	fprintf(debug_log, "FATAL: Received signal %d...dying.\n", sig);

#ifdef PRINT_STACK_TRACE
#ifdef _SQUID_HPUX_
    {
	extern void U_STACK_TRACE(void);	/* link with -lcl */
	fflush(debug_log);
	dup2(fileno(debug_log), 2);
	U_STACK_TRACE();
    }
#endif /* _SQUID_HPUX_ */
#ifdef _SQUID_SOLARIS_
    {				/* get ftp://opcom.sun.ca/pub/tars/opcom_stack.tar.gz and */
	extern void opcom_stack_trace(void);	/* link with -lopcom_stack */
	fflush(debug_log);
	dup2(fileno(debug_log), fileno(stdout));
	opcom_stack_trace();
	fflush(stdout);
    }
#endif /* _SQUID_SOLARIS_ */
#if HAVE_BACKTRACE_SYMBOLS_FD
    {
	static void *(callarray[8192]);
	int n;
	n = backtrace(callarray, 8192);
	backtrace_symbols_fd(callarray, n, fileno(debug_log));
    }
#endif
#endif /* PRINT_STACK_TRACE */

#if SA_RESETHAND == 0 && !defined(_SQUID_MSWIN_)
    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    signal(sig, SIG_DFL);
#endif
    releaseServerSockets();
    storeDirWriteCleanLogs(0);
    PrintRusage();
    dumpMallocStats();
    if (squid_curtime - SQUID_RELEASE_TIME < 864000) {
	/* skip if more than 10 days old */
	if (Config.adminEmail)
	    mail_warranty();
	else
	    puts(dead_msg());
    }
    abort();
}


void
sigusr2_handle(int sig)
{
    static int state = 0;

	DebugSignal = sig;
	
    /* no debug() here; bad things happen if the signal is delivered during _db_print() */
    if (state == 0) {
#ifndef MEM_GEN_TRACE
	_db_init("ALL,10");
	_db_init_log(Config.Log.log);
#else
	log_trace_done();
#endif
	state = 1;
    } else {
#ifndef MEM_GEN_TRACE
	_db_init(Config.debugOptions);
	_db_init_log(Config.Log.log);
#else
	log_trace_init("/tmp/squid.alloc");
#endif
	state = 0;
    }
#if !HAVE_SIGACTION
    signal(sig, sigusr2_handle);	/* reinstall */
#endif
}

static void
fatal_common(const char *message)
{
#if HAVE_SYSLOG
    syslog(LOG_ALERT, "%s", message);
#endif
    fprintf(debug_log, "FATAL: %s\n", message);
    if (opt_debug_stderr > 0 && debug_log != stderr)
	fprintf(stderr, "FATAL: %s\n", message);
    fprintf(debug_log, "Squid Cache (Version %s): Terminated abnormally.\n",
	version_string);
    fflush(debug_log);
    if (!shutting_down) {
	PrintRusage();
	dumpMallocStats();
#ifdef PRINT_STACK_TRACE
#ifdef _SQUID_HPUX_
	extern void U_STACK_TRACE(void);	/* link with -lcl */
	fflush(debug_log);
	dup2(fileno(debug_log), 2);
	U_STACK_TRACE();
#endif /* _SQUID_HPUX_ */
#ifdef _SQUID_SOLARIS_
	extern void opcom_stack_trace(void);	/* link with -lopcom_stack */
	fflush(debug_log);
	dup2(fileno(debug_log), fileno(stdout));
	opcom_stack_trace();
	fflush(stdout);
#endif /* _SQUID_SOLARIS_ */
#if HAVE_BACKTRACE_SYMBOLS_FD
	static void *(callarray[8192]);
	int n;
	n = backtrace(callarray, 8192);
	backtrace_symbols_fd(callarray, n, fileno(debug_log));
#endif
#endif /* PRINT_STACK_TRACE */
    }
}

/* fatal */
void
fatal(const char *message)
{
    leave_suid();
    releaseServerSockets();
    /* check for store_dirs_rebuilding because fatal() is often
     * used in early initialization phases, long before we ever
     * get to the store log. */
    if (0 == store_dirs_rebuilding)
	storeDirWriteCleanLogs(0);
    fatal_common(message);
    if (shutting_down)
	exit(1);
    else
	abort();
}

/* printf-style interface for fatal */
#if STDC_HEADERS
void
fatalf(const char *fmt,...)
{
    va_list args;
    va_start(args, fmt);
#else
void
fatalf(va_alist)
     va_dcl
{
    va_list args;
    const char *fmt = NULL;
    va_start(args);
    fmt = va_arg(args, char *);
#endif
    fatalvf(fmt, args);
    va_end(args);
}


/* used by fatalf */
void
fatalvf(const char *fmt, va_list args)
{
    static char fatal_str[BUFSIZ];
    vsnprintf(fatal_str, sizeof(fatal_str), fmt, args);
    fatal(fatal_str);
}

/* fatal with dumping core */
void
fatal_dump(const char *message)
{
    leave_suid();
    failure_notify = NULL;
    releaseServerSockets();
    if (message)
	fatal_common(message);
    if (opt_catch_signals)
	storeDirWriteCleanLogs(0);
    abort();
}

void
debug_trap(const char *message)
{
    if (!opt_catch_signals)
	fatal_dump(message);
    _db_print(GSkipBuildPrefix(__FILE__),__LINE__,__FUNCTION__,"WARNING: %s\n", message);
}

void
sig_child(int sig)
{
#ifndef _SQUID_MSWIN_
#ifdef _SQUID_NEXT_
    union wait status;
#else
    int status;
#endif
    pid_t pid;

    do {
#ifdef _SQUID_NEXT_
	pid = wait3(&status, WNOHANG, NULL);
#else
	pid = waitpid(-1, &status, WNOHANG);
#endif
	/* no debug() here; bad things happen if the signal is delivered during _db_print() */
#if HAVE_SIGACTION
    } while (pid > 0);
#else
    } while (pid > 0 || (pid < 0 && errno == EINTR));
    signal(sig, sig_child);
#endif
#endif /* _SQUID_MSWIN_ */
}

const char *
getMyHostname(void)
{
    LOCAL_ARRAY(char, host, SQUIDHOSTNAMELEN + 1);
    static int present = 0;
    const struct hostent *h = NULL;
    struct in_addr sa;
    if (Config.visibleHostname != NULL)
	return Config.visibleHostname;
    if (present)
	return host;
    host[0] = '\0';
    SetAnyAddr(&sa);
    if (Config.Sockaddr.http && IsAnyAddr(&sa))
	memcpy(&sa, &Config.Sockaddr.http->s.sin_addr, sizeof(sa));
#if USE_SSL
    if (Config.Sockaddr.https && IsAnyAddr(&sa))
	memcpy(&sa, &Config.Sockaddr.https->http.s.sin_addr, sizeof(sa));
#endif
    /*
     * If the first http_port address has a specific address, try a
     * reverse DNS lookup on it.
     */
    if (! IsAnyAddr(&sa)) {
	h = gethostbyaddr((char *) &sa,
	    sizeof(sa), AF_INET);
	if (h != NULL) {
	    /* DNS lookup successful */
	    /* use the official name from DNS lookup */
	    xstrncpy(host, h->h_name, SQUIDHOSTNAMELEN);
	    debugs(50, 4, "getMyHostname: resolved %s to '%s'",
		inet_ntoa(sa),
		host);
	    present = 1;
	    if (strchr(host, '.'))
		return host;

	}
	debugs(50, 1, "WARNING: failed to resolve %s to a fully qualified hostname",
	    inet_ntoa(sa));
    }
    /*
     * Get the host name and store it in host to return
     */
    if (gethostname(host, SQUIDHOSTNAMELEN) < 0) {
	debugs(50, 1, "WARNING: gethostname failed: %s", xstrerror());
    } else if ((h = gethostbyname(host)) == NULL) {
	debugs(50, 1, "WARNING: gethostbyname failed for %s", host);
    } else {
	debugs(50, 6, "getMyHostname: '%s' resolved into '%s'",
	    host, h->h_name);
	/* DNS lookup successful */
	/* use the official name from DNS lookup */
	xstrncpy(host, h->h_name, SQUIDHOSTNAMELEN);
	present = 1;
	if (strchr(host, '.'))
	    return host;
    }
    if (opt_send_signal == -1)
	debugs(50, 1, "Could not determine fully qualified hostname.  Please set 'visible_hostname'");
    if (host[0])
	return host;
    else
	return "localhost";
}

const char *
uniqueHostname(void)
{
    return Config.uniqueHostname ? Config.uniqueHostname : getMyHostname();
}

void
safeunlink(const char *s, int quiet)
{
    CommStats.syscalls.disk.unlinks++;
    if (unlink(s) < 0 && !quiet)
	debugs(50, 1, "safeunlink: Couldn't delete %s: %s", s, xstrerror());
}

/* Should get called after any operation which may make the OS disable core dumps */
void
enableCoredumps(void)
{
    if (!Config.coredump_dir)
	return;
    if (strcmp(Config.coredump_dir, "none") == 0)
	return;

#if HAVE_PRCTL && defined(PR_SET_DUMPABLE)
    /* Set Linux DUMPABLE flag */
    if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) != 0)
	debugs(50, 2, "prctl: %s", xstrerror());
#endif
#if HAVE_SETRLIMIT && defined(RLIMIT_CORE)
    /* Make sure coredumps are not limited */
    {
	struct rlimit rlim;

	if (getrlimit(RLIMIT_CORE, &rlim) == 0) {
	    rlim.rlim_cur = rlim.rlim_max;
	    setrlimit(RLIMIT_CORE, &rlim);
	}
    }
#endif
}

/* leave a privilegied section. (Give up any privilegies)
 * Routines that need privilegies can rap themselves in enter_suid()
 * and leave_suid()
 * To give upp all posibilites to gain privilegies use no_suid()
 */
void
leave_suid(void)
{
    debugs(21, 3, "leave_suid: PID %d called", (int) getpid());
    if (Config.effectiveGroup) {
#if HAVE_SETGROUPS
	setgroups(1, &Config2.effectiveGroupID);
#endif
	if (setgid(Config2.effectiveGroupID) < 0)
	    debugs(50, 0, "ALERT: setgid: %s", xstrerror());
    }
    if (geteuid() != 0)
	return;
    /* Started as a root, check suid option */
    if (Config.effectiveUser == NULL)
	return;
    debugs(21, 3, "leave_suid: PID %d giving up root, becoming '%s'",
	(int) getpid(), Config.effectiveUser);
    if (!Config.effectiveGroup) {
	if (setgid(Config2.effectiveGroupID) < 0)
	    debugs(50, 0, "ALERT: setgid: %s", xstrerror());
	if (initgroups(Config.effectiveUser, Config2.effectiveGroupID) < 0) {
	    debugs(50, 0, "ALERT: initgroups: unable to set groups for User %s "
		"and Group %u", Config.effectiveUser,
		(unsigned) Config2.effectiveGroupID);
	}
    }
#if HAVE_SETRESUID
    if (setresuid(Config2.effectiveUserID, Config2.effectiveUserID, 0) < 0)
	debugs(50, 0, "ALERT: setresuid: %s", xstrerror());
#elif HAVE_SETEUID
    if (seteuid(Config2.effectiveUserID) < 0)
	debugs(50, 0, "ALERT: seteuid: %s", xstrerror());
#else
    if (setuid(Config2.effectiveUserID) < 0)
	debugs(50, 0, "ALERT: setuid: %s", xstrerror());
#endif
    restoreCapabilities(1);
    /* Changing user ID usually blocks core dumps. Get them back! */
    enableCoredumps();
}

/* Enter a privilegied section */
void
enter_suid(void)
{
    debugs(21, 3, "enter_suid: PID %d taking root priveleges", (int) getpid());
#if HAVE_SETRESUID
    setresuid(-1, 0, -1);
#else
    setuid(0);
#endif
    enableCoredumps();
}

/* Give up the posibility to gain privilegies.
 * this should be used before starting a sub process
 */
void
no_suid(void)
{
    uid_t uid;
    leave_suid();
    uid = geteuid();
    debugs(21, 3, "no_suid: PID %d giving up root priveleges forever", (int) getpid());
    setuid(0);
    if (setuid(uid) < 0)
	debugs(50, 1, "no_suid: setuid: %s", xstrerror());
    restoreCapabilities(0);
    enableCoredumps();
}

void
writePidFile(void)
{
    int fd;
    const char *f = NULL;
    mode_t old_umask;
    char buf[32];
	
    if (!IamPrimaryProcess())
        return;
	
    if ((f = Config.pidFilename) == NULL)
	return;
    if (!strcmp(Config.pidFilename, "none"))
	return;
    enter_suid();
    old_umask = umask(022);
    fd = file_open(f, O_WRONLY | O_CREAT | O_TRUNC | O_TEXT);
    umask(old_umask);
    leave_suid();
    if (fd < 0) {
	debugs(50, 0, "%s: %s", f, xstrerror());
	debug_trap("Could not write pid file");
	return;
    }
    snprintf(buf, 32, "%d\n", (int) getpid());
    FD_WRITE_METHOD(fd, buf, strlen(buf));
    file_close(fd);
}


pid_t
readPidFile(void)
{
    FILE *pid_fp = NULL;
    const char *f = Config.pidFilename;
    char *chroot_f = NULL;
    pid_t pid = -1;
    int i;

    if (f == NULL || !strcmp(Config.pidFilename, "none")) {
	fprintf(stderr, "%s: ERROR: No pid file name defined\n", appname);
	exit(1);
    }
    if (Config.chroot_dir && geteuid() == 0) {
	int len = strlen(Config.chroot_dir) + 1 + strlen(f) + 1;
	chroot_f = xmalloc(len);
	snprintf(chroot_f, len, "%s/%s", Config.chroot_dir, f);
	f = chroot_f;
    }
    pid_fp = fopen(f, "r");
    if (pid_fp != NULL) {
	pid = 0;
	if (fscanf(pid_fp, "%d", &i) == 1)
	    pid = (pid_t) i;
	fclose(pid_fp);
    } else {
	if (errno != ENOENT) {
	    fprintf(stderr, "%s: ERROR: Could not read pid file\n", appname);
	    fprintf(stderr, "\t%s: %s\n", f, xstrerror());
	    exit(1);
	}
    }
    safe_free(chroot_f);
    return pid;
}


/* A little piece of glue for odd systems */
#ifndef RLIMIT_NOFILE
#ifdef RLIMIT_OFILE
#define RLIMIT_NOFILE RLIMIT_OFILE
#endif
#endif

/** Figure out the number of supported filedescriptors */
void
setMaxFD(void)
{
#if HAVE_SETRLIMIT && defined(RLIMIT_NOFILE)

    /* On Linux with 64-bit file support the sys/resource.h header
     * uses #define to change the function definition to require rlimit64
     */
#if defined(getrlimit)
    struct rlimit64 rl; // Assume its a 64-bit redefine anyways.
#else
    struct rlimit rl;
#endif

    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
        debugs(50, DBG_CRITICAL, "getrlimit: RLIMIT_NOFILE: %s", xstrerror());
    } else if (Config.max_filedescriptors > 0) {
#if USE_SELECT || USE_SELECT_WIN32
        /* select() breaks if this gets set too big */
        if (Config.max_filedescriptors > FD_SETSIZE) {
            rl.rlim_cur = FD_SETSIZE;
            debugs(50, DBG_CRITICAL, "WARNING: 'max_filedescriptors %d does not work with select()", Config.max_filedescriptors);
        } else
#endif
            rl.rlim_cur = Config.max_filedescriptors;
        if (rl.rlim_cur > rl.rlim_max)
            rl.rlim_max = rl.rlim_cur;
        if (setrlimit(RLIMIT_NOFILE, &rl)) {
            debugs(50, DBG_CRITICAL, "ERROR: setrlimit: RLIMIT_NOFILE: %s", xstrerror());
            getrlimit(RLIMIT_NOFILE, &rl);
            rl.rlim_cur = rl.rlim_max;
            if (setrlimit(RLIMIT_NOFILE, &rl)) {
                debugs(50, DBG_CRITICAL, "ERROR: setrlimit: RLIMIT_NOFILE: %s", xstrerror());
            }
        }
    }
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
        debugs(50, DBG_CRITICAL, "ERROR: getrlimit: RLIMIT_NOFILE: %s", xstrerror());
    } else {
        Squid_MaxFD = rl.rlim_cur;
    }

#endif /* HAVE_SETRLIMIT */
}

void
setSystemLimits(void)
{
#if HAVE_SETRLIMIT && defined(RLIMIT_NOFILE) && !_SQUID_CYGWIN_
    /* limit system filedescriptors to our own limit */

    /* On Linux with 64-bit file support the sys/resource.h header
     * uses #define to change the function definition to require rlimit64
     */
#if defined(getrlimit)
    struct rlimit64 rl; // Assume its a 64-bit redefine anyways.
#else
    struct rlimit rl;
#endif

    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
        debugs(50, DBG_CRITICAL, "getrlimit: RLIMIT_NOFILE: %s", xstrerror());
    } else {
        rl.rlim_cur = Squid_MaxFD;
        if (setrlimit(RLIMIT_NOFILE, &rl) < 0) {
            snprintf(tmp_error_buf, ERROR_BUF_SZ, "setrlimit: RLIMIT_NOFILE: %s", xstrerror());
            fatal_dump(tmp_error_buf);
        }
    }
#endif /* HAVE_SETRLIMIT */

#if HAVE_SETRLIMIT && defined(RLIMIT_DATA) && !_SQUID_CYGWIN_
    if (getrlimit(RLIMIT_DATA, &rl) < 0) {
        debugs(50, DBG_CRITICAL, "getrlimit: RLIMIT_DATA: %s", xstrerror());
    } else if (rl.rlim_max > rl.rlim_cur) {
        rl.rlim_cur = rl.rlim_max;  /* set it to the max */

        if (setrlimit(RLIMIT_DATA, &rl) < 0) {
            snprintf(tmp_error_buf, ERROR_BUF_SZ, "setrlimit: RLIMIT_DATA: %s", xstrerror());
            fatal_dump(tmp_error_buf);
        }
    }
#endif /* RLIMIT_DATA */
    if (Config.max_filedescriptors > Squid_MaxFD) {
        debugs(50, DBG_IMPORTANT, "NOTICE: Could not increase the number of filedescriptors");
    }

#if HAVE_SETRLIMIT && defined(RLIMIT_VMEM) && !_SQUID_CYGWIN_
    if (getrlimit(RLIMIT_VMEM, &rl) < 0) {
        debugs(50, DBG_CRITICAL, "getrlimit: RLIMIT_VMEM: %s", xstrerror());
    } else if (rl.rlim_max > rl.rlim_cur) {
        rl.rlim_cur = rl.rlim_max;  /* set it to the max */

        if (setrlimit(RLIMIT_VMEM, &rl) < 0) {
            snprintf(tmp_error_buf, ERROR_BUF_SZ, "setrlimit: RLIMIT_VMEM: %s", xstrerror());
            fatal_dump(tmp_error_buf);
        }
    }
#endif /* RLIMIT_VMEM */
}

struct in_addr
inaddrFromHostent(const struct hostent *hp)
{
    struct in_addr s;
    xmemcpy(&s.s_addr, hp->h_addr, sizeof(s.s_addr));
    return s;
}

const char *
checkNullString(const char *p)
{
    return p ? p : "(NULL)";
}

dlink_node *
dlinkNodeNew()
{
    if (dlink_node_pool == NULL)
	dlink_node_pool = memPoolCreate("Dlink list nodes", sizeof(dlink_node));
    /* where should we call memPoolDestroy(dlink_node_pool); */
    return memPoolAlloc(dlink_node_pool);
}

/* the node needs to be unlinked FIRST */
void
dlinkNodeDelete(dlink_node * m)
{
    if (m == NULL)
	return;
    memPoolFree(dlink_node_pool, m);
}

void
debugObj(int section, int level, const char *label, void *obj, ObjPackMethod pm)
{
    MemBuf mb;
    Packer p;
    assert(label && obj && pm);
    memBufDefInit(&mb);
    packerToMemInit(&p, &mb);
    (*pm) (obj, &p);
    debugs(section, level, "%s%s", label, mb.buf);
    packerClean(&p);
    memBufClean(&mb);
}

/*
 * Same as rename(2) but complains if something goes wrong;
 * the caller is responsible for handing and explaining the 
 * consequences of errors.
 */
int
xrename(const char *from, const char *to)
{
    debugs(21, 2, "xrename: renaming %s to %s", from, to);
#if defined(_SQUID_OS2_) || defined(_SQUID_WIN32_)
    remove(to);
#endif
    if (0 == rename(from, to))
	return 0;
    debugs(21, errno == ENOENT ? 2 : 1, "xrename: Cannot rename %s to %s: %s",
	from, to, xstrerror());
    return -1;
}

int
stringHasCntl(const char *s)
{
    unsigned char c;
    while ((c = (unsigned char) *s++) != '\0') {
	if (c <= 0x1f)
	    return 1;
	if (c >= 0x7f && c <= 0x9f)
	    return 1;
    }
    return 0;
}

/*
 * isPowTen returns true if its argument is an integer power of
 * 10.  Its used for logging of certain error messages that can
 * occur often, but that we don't want to fill cache.log with.
 */
int
isPowTen(int count)
{
    double x = log(count) / log(10.0);
    if (0.0 != x - (double) (int) x)
	return 0;
    return 1;
}

void
parseEtcHosts(void)
{
    FILE *fp;
    char buf[1024];
    char buf2[512];
    char *nt = buf;
    char *lt = buf;

    if (NULL == Config.etcHostsPath)
	return;
    if (0 == strcmp(Config.etcHostsPath, "none"))
	return;
    fp = fopen(Config.etcHostsPath, "r");
    if (fp == NULL) {
	debugs(1, 1, "parseEtcHosts: %s: %s",
	    Config.etcHostsPath, xstrerror());
	return;
    }
#ifdef _SQUID_WIN32_
    setmode(fileno(fp), O_TEXT);
#endif
    while (fgets(buf, 1024, fp)) {	/* for each line */
	wordlist *hosts = NULL;
	char *addr;
	if (buf[0] == '#')	/* MS-windows likes to add comments */
	    continue;
	strtok(buf, "#");	/* chop everything following a comment marker */
	lt = buf;
	addr = buf;
	debugs(1, 5, "etc_hosts: line is '%s'", buf);
	nt = strpbrk(lt, w_space);
	if (nt == NULL)		/* empty line */
	    continue;
	*nt = '\0';		/* null-terminate the address */
	debugs(1, 5, "etc_hosts: address is '%s'", addr);
	lt = nt + 1;
	while ((nt = strpbrk(lt, w_space))) {
	    char *host = NULL;
	    if (nt == lt) {	/* multiple spaces */
		debugs(1, 5, "etc_hosts: multiple spaces, skipping");
		lt = nt + 1;
		continue;
	    }
	    *nt = '\0';
	    debugs(1, 5, "etc_hosts: got hostname '%s'", lt);
	    if (Config.appendDomain && !strchr(lt, '.')) {
		/* I know it's ugly, but it's only at reconfig */
		strncpy(buf2, lt, 512);
		strncat(buf2, Config.appendDomain, 512 - strlen(lt) - 1);
		host = buf2;
	    } else {
		host = lt;
	    }
	    if (ipcacheAddEntryFromHosts(host, addr) != 0)
		goto skip;	/* invalid address, continuing is useless */
	    wordlistAdd(&hosts, host);
	    lt = nt + 1;
	}
	fqdncacheAddEntryFromHosts(addr, hosts);
      skip:
	wordlistDestroy(&hosts);
    }
    fclose(fp);
}

/*
 * Similar to strtok, but has some rudimentary knowledge
 * of quoting
 */
char *
strwordtok(char *buf, char **t)
{
    unsigned char *word = NULL;
    unsigned char *p = (unsigned char *) buf;
    unsigned char *d;
    unsigned char ch;
    int quoted = 0;
    if (!p)
	p = (unsigned char *) *t;
    if (!p)
	goto error;
    while (*p && xisspace(*p))
	p++;
    if (!*p)
	goto error;
    word = d = p;
    while ((ch = *p)) {
	switch (ch) {
	case '\\':
	    p++;
	    switch (*p) {
	    case 'n':
		ch = '\n';
		break;
	    case 'r':
		ch = '\r';
		break;
	    default:
		ch = *p;
		break;
	    }
	    *d++ = ch;
	    if (ch)
		p++;
	    break;
	case '"':
	    quoted = !quoted;
	    p++;
	    break;
	default:
	    if (!quoted && xisspace(*p)) {
		p++;
		goto done;
	    }
	    *d++ = *p++;
	    break;
	}
    }
  done:
    *d++ = '\0';
  error:
    *t = (char *) p;
    return (char *) word;
}

/*
 * Inverse of strwordtok. Quotes a word if needed
 */
void
strwordquote(MemBuf * mb, const char *str)
{
    int quoted = 0;
    if (strchr(str, ' ')) {
	quoted = 1;
	memBufAppend(mb, "\"", 1);
    }
    while (*str) {
	int l = strcspn(str, "\"\\");
	memBufAppend(mb, str, l);
	str += l;
	switch (*str) {
	case '\n':
	    memBufAppend(mb, "\\n", 2);
	    str++;
	    break;
	case '\r':
	    memBufAppend(mb, "\\r", 2);
	    str++;
	    break;
	case '\0':
	    break;
	default:
	    memBufAppend(mb, "\\", 1);
	    memBufAppend(mb, str, 1);
	    str++;
	    break;
	}
    }
    if (quoted)
	memBufAppend(mb, "\"", 1);
}

int
getMyPort(void)
{
    if (Config.Sockaddr.http)
	return ntohs(Config.Sockaddr.http->s.sin_port);
#if USE_SSL
    if (Config.Sockaddr.https)
	return ntohs(Config.Sockaddr.https->http.s.sin_port);
#endif
    fatal("No port defined");
    return 0;			/* NOT REACHED */
}

/*
 * Set the umask to at least the given mask. This is in addition
 * to the umask set at startup
 */
void
setUmask(mode_t mask)
{
    static mode_t orig_umask = (mode_t) ~ 0;
    if (orig_umask == (mode_t) ~ 0) {
	/* Unfortunately, there is no way to get the current
	 * umask value without setting it.
	 */
	orig_umask = umask(mask);
    }
    umask(mask | orig_umask);
}

void
keepCapabilities(void)
{
    comm_ips_keepCapabilities();
}

static void
restoreCapabilities(int keep)
{
    comm_ips_restoreCapabilities();
}

/**
 * Parse a socket address (host:port), fill the given sockaddr_in structure
 * Returns FALSE on failure, TRUE on success
 * Destroys s
 */
int
parse_sockaddr(char *s, struct sockaddr_in *addr)
{
    char *host, *tmp, *colon;
    unsigned short port = 0;
    const struct hostent *hp;

    host = NULL;
    port = 0;
    if ((colon = strchr(s, ':'))) {
	/* host:port */
	host = s;
	*colon = '\0';
	port = xatos(colon + 1);
	if (0 == port)
	    return FALSE;
    } else if ((port = strtol(s, &tmp, 10)), !*tmp) {
	/* port */
    } else {
	host = s;
	port = 0;
    }
    addr->sin_port = htons(port);
    if (NULL == host)
	SetAnyAddr(&addr->sin_addr);
    else if (1 == safe_inet_addr(host, &addr->sin_addr))
	(void) 0;
    else if ((hp = gethostbyname(host)))	/* dont use ipcache */
	addr->sin_addr = inaddrFromHostent(hp);
    else
	return FALSE;
    addr->sin_family = AF_INET;
    return TRUE;
}
