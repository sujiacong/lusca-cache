
/*
 * $Id: main.c 14832 2010-12-13 11:05:10Z roelf.diedericks $
 *
 * DEBUG: section 1     Startup and Main Loop
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

#include "squid.h"
#include "icmp.h"
#include "client_db.h"
#include "pconn.h"

#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
#include <windows.h>
#include <process.h>
static int opt_install_service = FALSE;
static int opt_remove_service = FALSE;
static int opt_signal_service = FALSE;
static int opt_command_line = FALSE;
extern void WIN32_svcstatusupdate(DWORD, DWORD);
void WINAPI WIN32_svcHandler(DWORD);
#endif

#include "../libmutiprocess/ipcsupport.h"

/* for error reporting from xmalloc and friends */
extern void (*failure_notify) (const char *);

static char *opt_syslog_facility = NULL;
static int icpPortNumOverride = 1;	/* Want to detect "-u 0" */
static int configured_once = 0;
#if MALLOC_DBG
static int malloc_debug_level = 0;
#endif
static volatile int do_reconfigure = 0;
static volatile int do_rotate = 0;
static volatile int do_shutdown = 0;
static volatile int shutdown_status = 0;

static int RotateSignal = -1;
static int ReconfigureSignal = -1;
static int ShutdownSignal = -1;

extern struct _fde_disk *fde_disk;

extern void closeHttpPortIfNeeded(http_port_list* oldport, http_port_list* newport);
#if USE_SSL
extern void closeHttpsPortIfNeeded(http_port_list* oldport, http_port_list* newport);
#endif

static void mainRotate(void);
static void mainReconfigureStart(void);
static void mainReconfigureFinish(void*);
static void mainInitialize(void);
static void usage(void);
static void mainParseOptions(int, char **);
static void sig_shutdown(int sig);
static void sendSignal(void);
static void serverConnectionsOpen(void);
static void watch_child(char **);
static void setEffectiveUser(void);
#if MEM_GEN_TRACE
extern void log_trace_done();
extern void log_trace_init(char *);
#endif
static EVH SquidShutdown;
static void mainSetCwd(void);
static int checkRunningPid(void);


static void doShutdown();


#ifndef _SQUID_MSWIN_
static const char *squid_start_script = "squid_start";
#endif

#if TEST_ACCESS
#include "test_access.c"
#endif

static void
usage(void)
{
    fprintf(stderr,
#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
	"Usage: %s [-hirvzCDFIRYX] [-d level] [-s | -l facility] [-f config-file] [-u port] [-k signal] [-n name] [-O command-line]\n"
#else
	"Usage: %s [-hvzCDFINRYX] [-d level] [-s | -l facility] [-f config-file] [-u port] [-k signal]\n"
#endif
	"       -d level  Write debugging to stderr also.\n"
	"       -f file   Use given config-file instead of\n"
	"                 %s\n"
	"       -h        Print help message.\n"
#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
	"       -i        Installs as a Windows Service (see -n option).\n"
#endif
	"       -k reconfigure|rotate|shutdown|interrupt|kill|debug|check|parse\n"
	"                 Parse configuration file, then send signal to \n"
	"                 running copy (except -k parse) and exit.\n"
#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
	"       -n name   Specify Windows Service name to use for service operations\n"
	"                 default is: " _WIN_SQUID_DEFAULT_SERVICE_NAME ".\n"
	"       -r        Removes a Windows Service (see -n option).\n"
#endif
	"       -s | -l facility\n"
	"                 Enable logging to syslog.\n"
	"       -u port   Specify ICP port number (default: %d), disable with 0.\n"
	"       -v        Print version.\n"
	"       -z        Create swap directories\n"
	"       -C        Do not catch fatal signals.\n"
	"       -D        Disable initial DNS tests.\n"
	"       -F        Don't serve any requests until store is rebuilt.\n"
	"       -N        No daemon mode.\n"
#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
	"       -O options\n"
	"                 Set Windows Service Command line options in Registry.\n"
#endif
	"       -R        Do not set REUSEADDR on port.\n"
	"       -S        Double-check swap during rebuild.\n"
	"       -X        Force full debugging.\n"
	"       -Y        Only return UDP_HIT or UDP_MISS_NOFETCH during fast reload.\n",
	appname, DefaultConfigFile, CACHE_ICP_PORT);
    exit(1);
}

void doShutdown()
{
	time_t wait = do_shutdown > 0 ? (int) Config.shutdownLifetime : 0;
	debugs(1, 1, "Preparing for shutdown after %d requests",
	statCounter.client_http.requests);
	debugs(1, 1, "Waiting %d seconds for active connections to finish",
	(int) wait);
	do_shutdown = 0;
	shutting_down = 1;
#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
	WIN32_svcstatusupdate(SERVICE_STOP_PENDING, (wait + 1) * 1000);
#endif
	serverConnectionsClose();
	eventAdd("SquidShutdown", SquidShutdown, NULL, (double) (wait + 1), 1);
}

int broadCastSignal()
{
    CoordinatorBroadcastSignal(DebugSignal);
    CoordinatorBroadcastSignal(RotateSignal);
    CoordinatorBroadcastSignal(ReconfigureSignal);
    CoordinatorBroadcastSignal(ShutdownSignal);

	DebugSignal = -1;
	RotateSignal = -1;
	ReconfigureSignal = -1;
	ShutdownSignal = -1;
	
    return -1;
}

static void
mainParseOptions(int argc, char *argv[])
{
    extern char *optarg;
    int c;

#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
    while ((c = getopt(argc, argv, "CDFIO:RSYXd:f:hik:m::n:rsl:u:vz?")) != -1) {
#else
    while ((c = getopt(argc, argv, "CDFINRSYXd:f:hk:m::sl:u:vz?")) != -1) {
#endif
	switch (c) {
	case 'C':
	    opt_catch_signals = 0;
	    break;
	case 'D':
	    opt_dns_tests = 0;
	    break;
	case 'F':
	    opt_foreground_rebuild = 1;
	    break;
	case 'N':
	    opt_no_daemon = 1;
	    break;
#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
	case 'O':
	    opt_command_line = 1;
	    WIN32_Command_Line = xstrdup(optarg);
	    break;
#endif
	case 'R':
	    opt_reuseaddr = 0;
	    break;
	case 'S':
	    opt_store_doublecheck = 1;
	    break;
	case 'X':
	    /* force full debugging */
	    sigusr2_handle(SIGUSR2);
	    break;
	case 'Y':
	    opt_reload_hit_only = 1;
	    break;
	case 'd':
	    opt_debug_stderr = atoi(optarg);
	    break;
	case 'f':
	    xfree(ConfigFile);
	    ConfigFile = xstrdup(optarg);
	    break;
	case 'h':
	    usage();
	    break;
#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
	case 'i':
	    opt_install_service = TRUE;
	    break;
#endif
	case 'k':
	    if ((int) strlen(optarg) < 1)
		usage();
	    if (!strncmp(optarg, "reconfigure", strlen(optarg)))
		opt_send_signal = SIGHUP;
	    else if (!strncmp(optarg, "rotate", strlen(optarg)))
#ifdef _SQUID_LINUX_THREADS_
		opt_send_signal = SIGQUIT;
#else
		opt_send_signal = SIGUSR1;
#endif
	    else if (!strncmp(optarg, "debug", strlen(optarg)))
#ifdef _SQUID_LINUX_THREADS_
		opt_send_signal = SIGTRAP;
#else
		opt_send_signal = SIGUSR2;
#endif
	    else if (!strncmp(optarg, "shutdown", strlen(optarg)))
		opt_send_signal = SIGTERM;
	    else if (!strncmp(optarg, "interrupt", strlen(optarg)))
		opt_send_signal = SIGINT;
	    else if (!strncmp(optarg, "kill", strlen(optarg)))
		opt_send_signal = SIGKILL;
	    else if (!strncmp(optarg, "check", strlen(optarg)))
		opt_send_signal = 0;	/* SIGNULL */
	    else if (!strncmp(optarg, "parse", strlen(optarg)))
		opt_parse_cfg_only = 1;		/* parse cfg file only */
	    else
		usage();
	    break;
	case 'm':
	    if (optarg) {
#if MALLOC_DBG
		malloc_debug_level = atoi(optarg);
		/* NOTREACHED */
		break;
#else
		fatal("Need to add -DMALLOC_DBG when compiling to use -mX option");
		/* NOTREACHED */
#endif
	    } else {
#if XMALLOC_TRACE
		xmalloc_trace = !xmalloc_trace;
#else
		fatal("Need to configure --enable-xmalloc-debug-trace to use -m option");
#endif
	    }
#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
	case 'n':
	    xfree(WIN32_Service_name);
	    WIN32_Service_name = xstrdup(optarg);
	    opt_signal_service = TRUE;
	    break;
	case 'r':
	    opt_remove_service = TRUE;
	    break;
#endif
	case 'l':
	    opt_syslog_facility = xstrdup(optarg);
	case 's':
#if HAVE_SYSLOG
	    _db_set_syslog(opt_syslog_facility);
	    break;
#else
	    fatal("Logging to syslog not available on this platform");
	    /* NOTREACHED */
#endif
	case 'u':
	    icpPortNumOverride = atoi(optarg);
	    if (icpPortNumOverride < 0)
		icpPortNumOverride = 0;
	    break;
	case 'v':
            printf("Squid Cache: Version %s\nconfigure options: %s\n", version_string, SQUID_CONFIGURE_OPTIONS);
#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
	    printf("Compiled as Windows System Service.\n");
#endif
	    exit(0);
	    /* NOTREACHED */
	case 'z':
	    opt_create_swap_dirs = 1;
	    break;
	case '?':
	default:
	    usage();
	    break;
	}
    }
}

/* ARGSUSED */
void
rotate_logs(int sig)
{
    do_rotate = 1;
	RotateSignal = sig;
#ifndef _SQUID_MSWIN_
#if !HAVE_SIGACTION
    signal(sig, rotate_logs);
#endif
#endif
}

/* ARGSUSED */
void
reconfigure(int sig)
{
    do_reconfigure = 1;
	ReconfigureSignal = sig;
#ifndef _SQUID_MSWIN_
#if !HAVE_SIGACTION
    signal(sig, reconfigure);
#endif
#endif
}

void
shut_down(int sig)
{
    do_shutdown = sig == SIGINT ? -1 : 1;
	ShutdownSignal = sig;
	
#if defined(SIGTTIN)
    if (SIGTTIN == sig)
        shutdown_status = 1;
#endif

#ifndef _SQUID_MSWIN_

	const pid_t ppid = getppid();

	if (!IamMasterProcess() && ppid > 1) {
		debugs(1, 1, "Killing master process, pid %ld, sig:%d", ppid, SIGUSR1);	
		// notify master that we are shutting down
		if (kill(ppid, SIGUSR1) < 0)
			debugs(1, 1, "Failed to send SIGUSR1 to master process, pid:%d, error:%s",ppid, xstrerror());
	}

#ifdef KILL_PARENT_OPT
    if (!IamMasterProcess() && ppid > 1) {
	debugs(1, 1, "Killing master process, pid %ld, sig:%d", ppid, sig);	
	if (kill(ppid, sig) < 0)
	    debugs(1, 1, "Failed to send SIGUSR1 to master process pid:%ld, error:%s", ppid, xstrerror());
    }
	
#endif
#if SA_RESETHAND == 0
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
#endif
#endif
}

static void
serverConnectionsOpen(void)
{
	if (IamPrimaryProcess()) 
	{
	
#if USE_WCCP
		wccpConnectionOpen();
#endif
	
#if USE_WCCPv2
		wccp2ConnectionOpen();
#endif
	}
	
	if (IamWorkerProcess()) {
	    clientOpenListenSockets();
	    icpConnectionsOpen();
#if USE_HTCP
	    htcpInit();
#endif
#ifdef SQUID_SNMP
	    snmpConnectionOpen();
#endif

	    clientdbInit();
	    icmpOpen();
	    netdbInit();
	    asnInit();
	    peerSelectInit();
	    carpInit();
	    peerSourceHashInit();
	    peerUserHashInit();
	    peerMonitorInit();
	}
}

void
serverConnectionsClose(void)
{
    assert(shutting_down || reconfiguring);
	if (IamPrimaryProcess()) 
	{
#if USE_WCCP
    	wccpConnectionClose();
#endif
#if USE_WCCPv2
    	wccp2ConnectionClose();
#endif
	}
	
	if (IamWorkerProcess()) 
	{
	    clientHttpConnectionsClose();
	    icpConnectionShutdown();
#if USE_HTCP
	    htcpSocketShutdown();
#endif
	    icmpClose();
#ifdef SQUID_SNMP
	    snmpConnectionShutdown();
#endif

	    asnFreeMemory();
	}
}

static void
mainReconfigureStart(void)
{
    debugs(1, 1, "Reconfiguring Squid Cache (version %s)...", version_string);
    reconfiguring = 1;
    /* Already called serverConnectionsClose and ipcacheShutdownServers() */
    serverConnectionsClose();
    icpConnectionClose();
#if USE_HTCP
    htcpSocketClose();
#endif
#ifdef SQUID_SNMP
    snmpConnectionClose();
#endif
    idnsShutdown();
    redirectShutdown();
    storeurlShutdown();
    locationRewriteShutdown();
    authenticateShutdown();
    externalAclShutdown();
    refreshCheckShutdown();
    if (! store_dirs_rebuilding)
        storeDirCloseSwapLogs();
    storeLogClose();
    accessLogClose();
    useragentLogClose();
    refererCloseLog();

	eventAdd("mainReconfigureFinish", mainReconfigureFinish, NULL, 0, 1);
}


static void
free_old_http_port_list(http_port_list ** head)
{
    http_port_list *s;
    while ((s = *head) != NULL) {
	*head = s->next;
	cbdataFree(s);
    }
}


#if USE_SSL
static void
free_old_https_port_list(https_port_list ** head)
{
    https_port_list *s;
    while ((s = *head) != NULL) {
	*head = (https_port_list *) (s->http.next);
	cbdataFree(s);
    }
}
#endif

static void
mainReconfigureFinish(void *data)
{
	http_port_list* oldHttpPorts = NULL;
#if USE_SSL	
	https_port_list* oldHttpsPorts = NULL;
#endif
	sqaddr_t ai, ao;
	debugs(1, 3, "finishing reconfiguring");
	
    errorClean();
    enter_suid();		/* root to read config file */

	// parse the config returns a count of errors encountered.
    const int oldWorkers = Config.workers;	

	if(IamCoordinatorProcess())
	{
		oldHttpPorts = Config.Sockaddr.http;	
		Config.Sockaddr.http = NULL;
#if USE_SSL			
		oldHttpsPorts = Config.Sockaddr.https;
		Config.Sockaddr.https = NULL;
#endif

	}
	
    parseConfigFile(ConfigFile);

	if(IamCoordinatorProcess())
	{
		closeHttpPortIfNeeded(oldHttpPorts, Config.Sockaddr.http);	
		free_old_http_port_list(&oldHttpPorts);	
#if USE_SSL		
		closeHttpsPortIfNeeded(oldHttpPorts, Config.Sockaddr.https);
		free_old_https_port_list(&oldHttpsPorts);	
#endif
	}

    if (oldWorkers != Config.workers) {
        debugs(1, 0, "WARNING: Changing 'workers' (from %d to %d) "
			"requires a full restart. It has been ignored by reconfigure.",oldWorkers,Config.workers);
        Config.workers = oldWorkers;
    }

    if (IamPrimaryProcess())
        CpuAffinityCheck();
    CpuAffinityReconfigure();

    /* XXX hacks for now to setup config options in libiapp; rethink this! -adrian */
    iapp_tcpRcvBufSz = Config.tcpRcvBufsz;
    iapp_useAcceptFilter = Config.accept_filter;
    iapp_incomingRate = Config.incoming_rate;
    httpConfig_relaxed_parser = Config.onoff.relaxed_header_parser;
    cfg_range_offset_limit = Config.rangeOffsetLimit;
#if USE_SSL
    ssl_engine = Config.SSL.ssl_engine;
    ssl_unclean_shutdown = Config.SSL.unclean_shutdown;
    ssl_password = Config.Program.ssl_password;
#endif
#if USE_IDENT
    identConfigTimeout(Config.Timeout.ident);
#endif
    opt_debug_log = Config.Log.log;
    opt_debug_rotate_count = Config.Log.rotateNumber;
    opt_debug_buffered_logs = Config.onoff.buffered_logs;

    setUmask(Config.umask);
    setEffectiveUser();
    _db_init(Config.debugOptions);
    _db_init_log(Config.Log.log);

    /* XXX the ipcache/fqdncache config variables need to be set before this is called! */
    ipcache_local_params();
    fqdncache_local_params();
    ipcache_restart();		/* clear stuck entries */
    fqdncache_restart();	/* sigh, fqdncache too */

    authenticateUserCacheRestart();	/* clear stuck ACL entries */
    parseEtcHosts();
    errorInitialize();		/* reload error pages */
    accessLogInit();
    storeLogOpen();
    useragentOpenLog();
    refererOpenLog();

    /* Setup internal DNS */
    sqinet_init(&ai);
    sqinet_init(&ao);
    sqinet_set_v4_inaddr(&ai, &Config.Addrs.udp_incoming);
    sqinet_set_v4_inaddr(&ao, &Config.Addrs.udp_outgoing);
    idnsConfigure(Config.onoff.ignore_unknown_nameservers, Config.Timeout.idns_retransmit, Config.Timeout.idns_query, Config.onoff.res_defnames);
    idnsConfigureV4Addresses(&ai, &ao);
    idnsConfigureV6Addresses(&Config.Addrs.udp_incoming6, &Config.Addrs.udp_outgoing6);
    sqinet_done(&ai);
    sqinet_done(&ao);
    idnsInit();
    idnsInternalInit();

    redirectInit();
    storeurlInit();
    locationRewriteInit();
    authenticateInit(&Config.authConfig);
    externalAclInit();
    refreshCheckInit();
	
if (IamPrimaryProcess()) 
	{	
#if USE_WCCP
	    wccpInit();
#endif
#if USE_WCCPv2
	    wccp2Init();
#endif
	}

#if DELAY_POOLS
    clientReassignDelaypools();
#endif
    serverConnectionsOpen();
    neighbors_init();
    if (! store_dirs_rebuilding)
        storeDirOpenSwapLogs();
    mimeInit(Config.mimeTablePathname);
    if (Config.onoff.announce) {
	if (!eventFind(start_announce, NULL))
	    eventAdd("start_announce", start_announce, NULL, 3600.0, 1);
    } else {
	if (eventFind(start_announce, NULL))
	    eventDelete(start_announce, NULL);
    }
    eventCleanup();
    writePidFile();		/* write PID file */
    debugs(1, 1, "Ready to serve requests.");
    reconfiguring = 0;

    // ignore any pending re-reconfigure signals if shutdown received
    if (do_shutdown)
        do_reconfigure = 0;	
}

static void
mainRotate(void)
{
    icmpClose();
    redirectShutdown();
    storeurlShutdown();
    locationRewriteShutdown();
    authenticateShutdown();
    externalAclShutdown();
    refreshCheckShutdown();
    _db_rotate_log();		/* cache.log */
    storeDirWriteCleanLogs(1);
    storeDirSync();		/* Flush pending I/O ops */
    storeLogRotate();		/* store.log */
    accessLogRotate();		/* access.log */
    useragentRotateLog();	/* useragent.log */
    refererRotateLog();		/* referer.log */
#if WIP_FWD_LOG
    fwdLogRotate();
#endif
    icmpOpen();
    redirectInit();
    storeurlInit();
    locationRewriteInit();
    authenticateInit(&Config.authConfig);
    externalAclInit();
    refreshCheckInit();
}

static void
setEffectiveUser(void)
{
    keepCapabilities();
    leave_suid();		/* Run as non privilegied user */
#ifdef _SQUID_OS2_
    return;
#endif
    if (geteuid() == 0) {
	debugs(0, 0, "Squid is not safe to run as root!  If you must");
	debugs(0, 0, "start Squid as root, then you must configure");
	debugs(0, 0, "it to run as a non-priveledged user with the");
	debugs(0, 0, "'cache_effective_user' option in the config file.");
	fatal("Don't run Squid as root, set 'cache_effective_user'!");
    }
}

static void
mainSetCwd(void)
{
    char pathbuf[MAXPATHLEN];
    if (Config.coredump_dir) {
	if (0 == strcmp("none", Config.coredump_dir)) {
	    (void) 0;
	} else if (chdir(Config.coredump_dir) == 0) {
	    debugs(0, 1, "Set Current Directory to %s", Config.coredump_dir);
	    return;
	} else {
	    debugs(50, 0, "chdir: %s: %s", Config.coredump_dir, xstrerror());
	}
    }
    /* If we don't have coredump_dir or couldn't cd there, report current dir */
    if (getcwd(pathbuf, MAXPATHLEN)) {
	debugs(0, 1, "Current Directory is %s", pathbuf);
    } else {
	debugs(50, 0, "WARNING: Can't find current directory, getcwd: %s", xstrerror());
    }
}

static void
mainInitialize(void)
{
	sqaddr_t ai, ao;
    /* chroot if configured to run inside chroot */
    if (Config.chroot_dir && (chroot(Config.chroot_dir) != 0 || chdir("/") != 0)) {
	fatal("failed to chroot");
    }
    if (opt_catch_signals) {
	squid_signal(SIGSEGV, death, SA_NODEFER | SA_RESETHAND);
	squid_signal(SIGBUS, death, SA_NODEFER | SA_RESETHAND);
    }
    squid_signal(SIGPIPE, SIG_IGN, SA_RESTART);
    squid_signal(SIGCHLD, sig_child, SA_NODEFER | SA_RESTART);

    setEffectiveUser();
    if (icpPortNumOverride != 1)
	Config.Port.icp = (u_short) icpPortNumOverride;

    _db_init(Config.debugOptions);
    _db_init_log(Config.Log.log);

    fd_open(fileno(debug_log), FD_LOG, Config.Log.log);
#if MEM_GEN_TRACE
    log_trace_init("/tmp/squid.alloc");
#endif
    debugs(1, 0, "Starting Squid Cache version %s for %s...",
	version_string,
	CONFIG_HOST_TYPE);

	debugs(1, 0, "CurrentKid,id:%d,kind:%d", KidIdentifier,TheProcessKind);

	CpuAffinityPrint();

#ifdef _SQUID_WIN32_
    if (WIN32_run_mode == _WIN_SQUID_RUN_MODE_SERVICE) {
	debugs(1, 0, "Running as %s Windows System Service on %s", WIN32_Service_name, WIN32_OS_string);
	debugs(1, 0, "Service command line is: %s", WIN32_Service_Command_Line);
    } else
	debugs(1, 0, "Running on %s", WIN32_OS_string);
#endif
    debugs(1, 1, "Process ID %d", (int) getpid());
    setSystemLimits();
    debugs(1, 1, "With %d file descriptors available", Squid_MaxFD);
#ifdef _SQUID_MSWIN_
    debugs(1, 1, "With %d CRT stdio descriptors available", _getmaxstdio());
    if (WIN32_Socks_initialized)
	debugs(1, 1, "Windows sockets initialized");
    if (WIN32_OS_version > _WIN_OS_WINNT) {
	WIN32_IpAddrChangeMonitorInit();
    }
#endif

    comm_select_postinit();
    if (!configured_once)
	disk_init();		/* disk_init must go before ipcache_init() */

    /* XXX the ipcache/fqdncache config variables need to be set before this is called! */
    ipcache_local_params();
    fqdncache_local_params();
    ipcache_init(Config.dns_testname_list);
    ipcache_init_local();
    fqdncache_init();
    fqdncache_init_local();

    parseEtcHosts();

    /* Setup internal DNS */
    sqinet_init(&ai);
    sqinet_init(&ao);
    sqinet_set_v4_inaddr(&ai, &Config.Addrs.udp_incoming);
    sqinet_set_v4_inaddr(&ao, &Config.Addrs.udp_outgoing);
    idnsConfigure(Config.onoff.ignore_unknown_nameservers, Config.Timeout.idns_retransmit, Config.Timeout.idns_query, Config.onoff.res_defnames);
    idnsConfigureV4Addresses(&ai, &ao);
    idnsConfigureV6Addresses(&Config.Addrs.udp_incoming6, &Config.Addrs.udp_outgoing6);
    sqinet_done(&ai);
    sqinet_done(&ao);
    idnsInit();
    idnsInternalInit();

    redirectInit();
    storeurlInit();
    locationRewriteInit();
    errorMapInit();
    authenticateInit(&Config.authConfig);
    externalAclInit();
    refreshCheckInit();
    useragentOpenLog();
    refererOpenLog();
    httpHeaderInitModule();	/* must go before any header processing (e.g. the one in errorInitialize) */
    httpReplyInitModule();	/* must go before accepting replies */
    errorInitialize();
    accessLogInit();
#if USE_IDENT
    identInit();
#endif
#ifdef SQUID_SNMP
    snmpInit();
#endif
#if MALLOC_DBG
    malloc_debug(0, malloc_debug_level);
#endif

    if (!configured_once) {
#if USE_UNLINKD
	unlinkdInit();
#endif
	urlInitialize();
	cachemgrInit();
	statInit();
	storeInit();
	mainSetCwd();
	/* after this point we want to see the mallinfo() output */
	do_mallinfo = 1;
	mimeInit(Config.mimeTablePathname);
	pconnInit();
	refreshInit();
#if DELAY_POOLS
	delayPoolsInit();
#endif
	fwdInit();
    }

	if (IamPrimaryProcess())	
	{
#if USE_WCCP
    wccpInit();
#endif
#if USE_WCCPv2
    wccp2Init();
#endif
	}

    serverConnectionsOpen();
	
    neighbors_init();
    if (Config.chroot_dir)
	no_suid();
    if (!configured_once)
	writePidFile();		/* write PID file */

#ifdef _SQUID_LINUX_THREADS_
    squid_signal(SIGQUIT, rotate_logs, SA_RESTART);
    squid_signal(SIGTRAP, sigusr2_handle, SA_RESTART);
#else
    squid_signal(SIGUSR1, rotate_logs, SA_RESTART);
    squid_signal(SIGUSR2, sigusr2_handle, SA_RESTART);
#endif
    squid_signal(SIGHUP, reconfigure, SA_RESTART);
    squid_signal(SIGTERM, shut_down, SA_NODEFER | SA_RESETHAND | SA_RESTART);
    squid_signal(SIGINT, shut_down, SA_NODEFER | SA_RESETHAND | SA_RESTART);
    memCheckInit();
    debugs(1, 1, "Ready to serve requests.");
    if (!configured_once) {
	eventAdd("storeMaintain", storeMaintainSwapSpace, NULL, 1.0, 1);
	if (Config.onoff.announce)
	    eventAdd("start_announce", start_announce, NULL, 3600.0, 1);
	eventAdd("ipcache_purgelru", ipcache_purgelru, NULL, 10.0, 1);
	eventAdd("fqdncache_purgelru", fqdncache_purgelru, NULL, 15.0, 1);
    }
    configured_once = 1;
}

#if USE_WIN32_SERVICE
/* When USE_WIN32_SERVICE is defined, the main function is placed in win32.c */
void WINAPI
SquidWinSvc
int argc, char **argv)
{
    SquidMain(argc, argv);
}

int
SquidMain(int argc, char **argv)
#else
int
main(int argc, char **argv)
#endif
{
	ConfigureCurrentKid(argv[0]);

    int errcount = 0;
    int loop_delay;
	int default_squid_maxfd;
#ifdef _SQUID_WIN32_
    int WIN32_init_err;
#endif

#if HAVE_SBRK
    sbrk_start = sbrk(0);
#endif

    debug_log = stderr;

#ifdef _SQUID_WIN32_
    if ((WIN32_init_err = WIN32_Subsystem_Init(&argc, &argv)))
	return WIN32_init_err;
#endif

    /* call mallopt() before anything else */
#if HAVE_MALLOPT
#ifdef M_GRAIN
    /* Round up all sizes to a multiple of this */
    mallopt(M_GRAIN, 16);
#endif
#ifdef M_MXFAST
    /* biggest size that is considered a small block */
    mallopt(M_MXFAST, 256);
#endif
#ifdef M_NBLKS
    /* allocate this many small blocks at once */
    mallopt(M_NLBLKS, 32);
#endif
#endif /* HAVE_MALLOPT */

    squid_srandom(time(NULL));

    getCurrentTime();
    squid_start = current_time;
    failure_notify = fatal_dump;

#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
    WIN32_svcstatusupdate(SERVICE_START_PENDING, 10000);
#endif
    mainParseOptions(argc, argv);

#if HAVE_SYSLOG && defined(LOG_LOCAL4)
    openlog(appname, LOG_PID | LOG_NDELAY | LOG_CONS, syslog_facility);
#endif

#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
    if (opt_install_service) {
	WIN32_InstallService();
	return 0;
    }
    if (opt_remove_service) {
	WIN32_RemoveService();
	return 0;
    }
    if (opt_command_line) {
	WIN32_SetServiceCommandLine();
	return 0;
    }
#endif

    /* parse configuration file
     * note: in "normal" case this used to be called from mainInitialize() */
    {
	int parse_err;
	if (!ConfigFile)
	    ConfigFile = xstrdup(DefaultConfigFile);
	assert(!configured_once);
#if USE_LEAKFINDER
	leakInit();
#endif
        libcore_set_fatalf(fatalvf);
	iapp_init();		/* required for configuration parsing */
	memInit();
	buf_init();
	cbdataLocalInit();
	eventLocalInit();
	storeFsInit();		/* required for config parsing */
	authenticateSchemeInit();	/* required for config parsing */

	default_squid_maxfd=Squid_MaxFD;
	parse_err = parseConfigFile(ConfigFile);

	if (opt_parse_cfg_only)
	    return parse_err;
	
        /* XXX hacks for now to setup config options in libiapp; rethink this! -adrian */
        iapp_tcpRcvBufSz = Config.tcpRcvBufsz;
        iapp_useAcceptFilter = Config.accept_filter;
        iapp_incomingRate = Config.incoming_rate;
        httpConfig_relaxed_parser = Config.onoff.relaxed_header_parser;
        cfg_range_offset_limit = Config.rangeOffsetLimit;
#if USE_SSL
        ssl_engine = Config.SSL.ssl_engine;
        ssl_unclean_shutdown = Config.SSL.unclean_shutdown;
        ssl_password = Config.Program.ssl_password;
#endif
#if USE_IDENT
        identConfigTimeout(Config.Timeout.ident);
#endif
        opt_debug_log = Config.Log.log;
        opt_debug_rotate_count = Config.Log.rotateNumber;
        opt_debug_buffered_logs = Config.onoff.buffered_logs;
    }
    setUmask(Config.umask);
    if (-1 == opt_send_signal)
	if (checkRunningPid())
	    exit(1);

	if (IamCoordinatorProcess())
		 initIpcCoordinatorInstance();
	 else if (UsingSmp() && (IamWorkerProcess() || IamDiskProcess()))
		 initIpcStrandInstance();

    /* Make sure the OS allows core dumps if enabled in squid.conf */
    enableCoredumps();

#if TEST_ACCESS
    comm_init();
    disk_init();
    comm_select_init();
    mainInitialize();
    test_access();
    return 0;
#endif

    /* send signal to running copy and exit */
    if (opt_send_signal != -1) {
	/* chroot if configured to run inside chroot */
	if (Config.chroot_dir) {
	    if (chroot(Config.chroot_dir))
		fatal("failed to chroot");
	    no_suid();
	} else {
	    leave_suid();
	}
	sendSignal();
	/* NOTREACHED */
    }

    if (!opt_no_daemon && Config.workers > 0)
	watch_child(argv);
	
    if (opt_create_swap_dirs) {
	/* chroot if configured to run inside chroot */
	if (Config.chroot_dir && chroot(Config.chroot_dir)) {
	    fatal("failed to chroot");
	}
	setEffectiveUser();
	debugs(0, 0, "Creating Swap Directories");
	storeCreateSwapDirectories();
	return 0;
    }
	
    if (IamPrimaryProcess())
        CpuAffinityCheck();
    CpuAffinityInit();

	setMaxFD();

	if(default_squid_maxfd != Squid_MaxFD)
	{
		 safe_free(fd_table);
		 fd_table = xcalloc(Squid_MaxFD, sizeof(fde));
		 RESERVED_FD = XMIN(100, Squid_MaxFD / 4);

		 safe_free(fde_disk);
		 fde_disk = xcalloc(Squid_MaxFD, sizeof(struct _fde_disk));

		comm_select_shutdown();
		
		comm_select_init();
	}
	
    if (opt_no_daemon) {
	/* we have to init fdstat here. */
	fd_open(0, FD_LOG, "stdin");
	fd_open(1, FD_LOG, "stdout");
	fd_open(2, FD_LOG, "stderr");
    }
#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
    WIN32_svcstatusupdate(SERVICE_START_PENDING, 10000);
#endif
    mainInitialize();

#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
    WIN32_svcstatusupdate(SERVICE_RUNNING, 0);
#endif

	if (IamCoordinatorProcess())
		 StartIpcCoordinatorInstance();
	 else if (UsingSmp() && (IamWorkerProcess() || IamDiskProcess()))
		 StartIpcStrandInstance();

    /* main loop */
    for (;;) {
	if (do_reconfigure) {
	    mainReconfigureStart();
	    do_reconfigure = 0;
	} else if (do_rotate) {
	    mainRotate();
	    do_rotate = 0;
	} else if (do_shutdown) {
		doShutdown();
	}

	broadCastSignal();
	
    /* Set a maximum loop delay; it'll be lowered elsewhere as appropriate */
	loop_delay = 60000;
	if (debug_log_flush() && loop_delay > 1000)
	    loop_delay = 1000;
	switch (iapp_runonce(loop_delay)) {
	case COMM_OK:
	    errcount = 0;	/* reset if successful */
	    break;
	case COMM_ERROR:
	    errcount++;
	    debugs(1, 0, "Select loop Error. Retry %d", errcount);
	    if (errcount == 10)
		fatal_dump("Select Loop failed!");
	    break;
	case COMM_TIMEOUT:
	    break;
	case COMM_SHUTDOWN:
	    SquidShutdown(NULL);
	    break;
	default:
	    fatal_dump("MAIN: Internal error -- this should never happen.");
	    break;
	}
        /* Check for disk io callbacks */
        storeDirCallback();
    }
    /* NOTREACHED */
    return 0;
}

static void
sig_shutdown(int sig)
{
    shutting_down = 1;
}

static void
sendSignal(void)
{
    pid_t pid;
    debug_log = stderr;
    pid = readPidFile();
    if (pid > 1) {
#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
	if (opt_signal_service)
	    WIN32_sendSignal(opt_send_signal);
	else {
#endif
#if defined(_SQUID_MSWIN_) && defined(USE_WIN32_SERVICE)
	    fprintf(stderr, "%s: ERROR: Could not send ", appname);
	    fprintf(stderr, "signal to Squid Service:\n");
	    fprintf(stderr, "missing -n command line switch.\n");
#else
	    if (kill(pid, opt_send_signal) &&
	    /* ignore permissions if just running check */
		!(opt_send_signal == 0 && errno == EPERM)) {
		fprintf(stderr, "%s: ERROR: Could not send ", appname);
		fprintf(stderr, "signal %d to process %d: %s\n",
		    opt_send_signal, (int) pid, xstrerror());
#endif
		exit(1);
	    }
#if defined(USE_WIN32_SERVICE) && defined(_SQUID_CYGWIN_)
	}
#endif
    } else {
	fprintf(stderr, "%s: ERROR: No running copy\n", appname);
	exit(1);
    }
    /* signal successfully sent */
    exit(0);
}

#ifndef _SQUID_MSWIN_
/*
 * This function is run when Squid is in daemon mode, just
 * before the parent forks and starts up the child process.
 * It can be used for admin-specific tasks, such as notifying
 * someone that Squid is (re)started.
 */
static void
mainStartScript(const char *prog)
{
    char script[SQUID_MAXPATHLEN];
    char *t;
    size_t sl = 0;
    pid_t cpid;
    pid_t rpid;
    xstrncpy(script, prog, MAXPATHLEN);
    if ((t = strrchr(script, '/'))) {
	*(++t) = '\0';
	sl = strlen(script);
    }
    xstrncpy(&script[sl], squid_start_script, MAXPATHLEN - sl);
    if ((cpid = fork()) == 0) {
	/* child */
	execl(script, squid_start_script, NULL);
	_exit(0);
    } else {
	do {
#ifdef _SQUID_NEXT_
	    union wait status;
	    rpid = wait3(&status, 0, NULL);
#else
	    int status;
	    rpid = waitpid(-1, &status, 0);
#endif
	} while (rpid != cpid);
    }
}
#endif

static int
checkRunningPid(void)
{
    pid_t pid;
    debug_log = stderr;
    if (strcmp(Config.pidFilename, "none") == 0) {
	debugs(0, 1, "No pid_filename specified. Trusting you know what you are doing.");
	return 0;
    }
    pid = readPidFile();
    if (pid < 2)
	return 0;
    if (kill(pid, 0) < 0)
	return 0;
    debugs(0, 0, "Squid is already running!  Process ID %ld", (long int) pid);
    return 1;
}

static void
watch_child(char *argv[])
{
#ifndef _SQUID_MSWIN_
    char *prog;
    //int failcount = 0;
    time_t start;
    time_t stop;
#ifdef _SQUID_NEXT_
    union wait status;
#else
    int status;
#endif
    pid_t pid;
#ifdef TIOCNOTTY
    int i;
#endif
    int nullfd;


    if (!IamMasterProcess())
        return;

    if (*(argv[0]) == '(')
	return;
	
    if ((pid = fork()) < 0)
	syslog(LOG_ALERT, "fork failed: %s", xstrerror());
    else if (pid > 0)
	exit(0);
	
    if (setsid() < 0)
	syslog(LOG_ALERT, "setsid failed: %s", xstrerror());
	
#ifdef TIOCNOTTY
    if ((i = open("/dev/tty", O_RDWR | O_TEXT)) >= 0) {
	ioctl(i, TIOCNOTTY, NULL);
	close(i);
    }
#endif


    /*
     * RBCOLLINS - if cygwin stackdumps when squid is run without
     * -N, check the cygwin1.dll version, it needs to be AT LEAST
     * 1.1.3.  execvp had a bit overflow error in a loop..
     */
    /* Connect stdio to /dev/null in daemon mode */
    nullfd = open(_PATH_DEVNULL, O_RDWR | O_TEXT);
    if (nullfd < 0)
	fatalf(_PATH_DEVNULL " %s\n", xstrerror());
	dup2(nullfd, 0);
    if (_db_stderr_debug_opt() < 0) {
	dup2(nullfd, 1);
	dup2(nullfd, 2);
    }
    if (nullfd > 2)
	close(nullfd);

	// handle shutdown notifications from kids
    squid_signal(SIGUSR1, sig_shutdown, SA_RESTART);

    if (Config.workers > 128) {
        syslog(LOG_ALERT, "Suspiciously high workers value: %d",
               Config.workers);
        // but we keep going in hope that user knows best
    }

	initAllkids(Config.workers);
	
	syslog(LOG_NOTICE, "Squid Parent: will start %d kids", (int)AllKids.kidcount);
	
    for (;;) {
		
	mainStartScript(argv[0]);

	// start each kid that needs to be [re]started; once
	for (i = AllKids.kidcount - 1; i >= 0; --i) {
		Kid* kid = &AllKids.storage[i];
		if (!kidShouldRestart(kid))
			continue;
	
		if ((pid = fork()) == 0) {
			/* child */
			openlog(APP_SHORTNAME, LOG_PID | LOG_NDELAY | LOG_CONS, LOG_LOCAL4);
			prog = argv[0];
			argv[0] = kid->theName;
			execvp(prog, argv);
			syslog(LOG_ALERT, "execvp failed: %s", xstrerror());
		}
	
		kidStart(kid, pid);
		syslog(LOG_NOTICE, "%s Parent: child process %d started", kid->theName, pid);
	}

	time(&start);
	squid_signal(SIGINT, SIG_IGN, SA_RESTART);
	
#ifdef _SQUID_NEXT_
	pid = wait3(&status, 0, NULL);
#else
	pid = waitpid(-1, &status, 0);
#endif
	time(&stop);

	if(pid < 0)
	{
		syslog(LOG_ALERT, "waitpid failed: %s", xstrerror());
		if(errno == ECHILD)
		{
			exit(2);
		}
	}
	
    // Loop to collect all stopped kids before we go to sleep below.
	do {
		Kid* kid = kidFind(pid);
		if (kid) {
			stopKid(kid,status);
			if (isKidCalledExit(kid)) {
				syslog(LOG_NOTICE,
					   "Squid Parent: %s process %d exited with status %d", kid->theName, kid->pid, kidExitStatus(kid));
			} else if (isKidSignaled(kid)) {
				syslog(LOG_NOTICE,
					   "Squid Parent: %s process %d exited due to signal %d with status %d", kid->theName, kid->pid, kidTermSignal(kid), kidExitStatus(kid));
			} else {
				syslog(LOG_NOTICE, "Squid Parent: %s process %d exited", kid->theName, kid->pid);
			}
			if (isKidHopeless(kid)) {
				syslog(LOG_NOTICE, "Squid Parent: %s process %d will not"
					   " be restarted due to repeated, frequent failures",kid->theName, kid->pid);
			}
			
			if(strstr(kid->theName,"squid-coord-") && !opt_create_swap_dirs)
			{
				syslog(LOG_NOTICE, "Squid Parent: squid-coord exited, Squid Parent exit");
				exit(3);
			}
		} else {
			syslog(LOG_NOTICE, "Squid Parent: unknown child process %d exited, Squid Parent exit", pid);
			exit(2);
		}
#if _SQUID_NEXT_
	} while ((pid = wait3(&status, WNOHANG, NULL)) > 0);
#else
	}
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0);
#endif

	if (!isKidsSomeRunning() && !isKidsShouldRestartSome()) {

		syslog(LOG_ALERT, "no kid running, Squid Parent exit");
		
		if (kidsSomeSignaledWithSig(SIGINT) || kidsSomeSignaledWithSig(SIGTERM)) {
			syslog(LOG_ALERT, "Exiting due to unexpected forced shutdown");
			exit(1);
		}

		if (isKidsAllHopeless()) {
			syslog(LOG_ALERT, "Exiting due to repeated, frequent failures");
			exit(1);
		}

		exit(0);
	}

	squid_signal(SIGINT, SIG_DFL, SA_RESTART);
	sleep(3);
    }
    /* NOTREACHED */
#endif
}

static void
SquidShutdown(void *unused)
{
#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
    WIN32_svcstatusupdate(SERVICE_STOP_PENDING, 10000);
#endif
    debugs(1, 1, "Shutting down...");
    idnsShutdown();
    redirectShutdown();
    storeurlShutdown();
    externalAclShutdown();
    refreshCheckShutdown();
    locationRewriteShutdown();
    icpConnectionClose();
#if USE_HTCP
    htcpSocketClose();
#endif
#ifdef SQUID_SNMP
    snmpConnectionClose();
#endif
#if USE_WCCP
    wccpConnectionClose();
#endif
#if USE_WCCPv2
    wccp2ConnectionClose();
#endif
    releaseServerSockets();
    commCloseAllSockets();
    authenticateShutdown();
#if defined(USE_WIN32_SERVICE) && defined(_SQUID_WIN32_)
    WIN32_svcstatusupdate(SERVICE_STOP_PENDING, 10000);
#endif
    storeDirSync();		/* Flush pending object writes/unlinks */
#if USE_UNLINKD
    unlinkdClose();		/* after storeDirSync! */
#endif
    storeDirWriteCleanLogs(0);
    PrintRusage();
    dumpMallocStats();
    storeDirSync();		/* Flush log writes */
    storeLogClose();
    accessLogClose();
    useragentLogClose();
    refererCloseLog();
#if WIP_FWD_LOG
    fwdUninit();
#endif
    storeDirSync();		/* Flush log close */
    storeFsDone();
 
#if LEAK_CHECK_MODE && 0 /* doesn't work at the moment */
    configFreeMemory();
    storeFreeMemory();
    /*stmemFreeMemory(); */
    netdbFreeMemory();
    ipcacheFreeMemory();
    fqdncacheFreeMemory();
    asnFreeMemory();
    clientdbFreeMemory();
    httpHeaderCleanModule();
    statFreeMemory();
    eventFreeMemory();
    mimeFreeMemory();
    errorClean();
#endif
#if !XMALLOC_TRACE
    if (opt_no_daemon) {
	fd_close(0);
	fd_close(1);
	fd_close(2);
    }
#endif
    comm_select_shutdown();
    fdDumpOpen();
    fdFreeMemory();
    memClean();
#if XMALLOC_TRACE
    xmalloc_find_leaks();
    debugs(1, 0, "Memory used after shutdown: %d", xmalloc_total);
#endif
#if MEM_GEN_TRACE
    log_trace_done();
#endif

	if (IamPrimaryProcess()) {
	    if (Config.pidFilename && strcmp(Config.pidFilename, "none") != 0) {
		enter_suid();
		safeunlink(Config.pidFilename, 0);
		leave_suid();
	    }
	}

    debugs(1, 1, "Squid Cache (Version %s): Exiting normally.",
	version_string);
	
    /*
     * DPW 2006-10-23
     * We used to fclose(debug_log) here if it was set, but then
     * we forgot to set it to NULL.  That caused some coredumps
     * because exit() ends up calling a bunch of destructors and
     * such.   So rather than forcing the debug_log to close, we'll
     * leave it open so that those destructors can write some
     * debugging if necessary.  The file will be closed anyway when
     * the process truly exits.
    */
    
    exit(shutdown_status);
}
