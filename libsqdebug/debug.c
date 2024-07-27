#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "../include/util.h"
#include "../libcore/varargs.h"
#include "debug.h"
#include "../libcore/tools.h"
#include "ctx.h"

/* XXX this library relies on the squid x* memory allocation/free routines */
#include "../include/util.h"


#include "debug_file.h"
#include "debug_syslog.h"

extern int shutting_down;
int opt_debug_stderr = -1;

int debugLevels[MAX_DEBUG_SECTIONS];
int _db_level;

char * _debug_options = NULL;

#define	MAX_DEBUG_CALLBACKS	16

static struct {
	int count;
	struct {
		DBG_MSG * cb;
		int do_timestamp;
	} cbs[MAX_DEBUG_CALLBACKS];
} db_callbacks = { 0 };

static const char *debugLogTime(time_t);

#ifdef _SQUID_MSWIN_
extern LPCRITICAL_SECTION dbg_mutex;
#endif

#ifdef _SQUID_LINUX_
/* Workaround for crappy glic header files */
extern int backtrace(void *, int);
extern void backtrace_symbols_fd(void *, int, int);
extern int setresuid(uid_t, uid_t, uid_t);
#endif /* _SQUID_LINUX */

void
_db_set_stderr_debug(int value)
{
	opt_debug_stderr = value;
}

int
_db_stderr_debug_opt(void)
{
	return opt_debug_stderr;
}

void
_db_register_handler(DBG_MSG *cb, int do_timestamp)
{
	int i;
	assert (db_callbacks.count < MAX_DEBUG_CALLBACKS);
	/* Don't re-register already registered callbacks */
	for (i = 0; i < db_callbacks.count; i++) {
		if (db_callbacks.cbs[i].cb == cb)
			return;
	}
	db_callbacks.cbs[db_callbacks.count].do_timestamp = do_timestamp;
	db_callbacks.cbs[db_callbacks.count++].cb = cb;
}

void
_db_unregister_all(void)
{
	db_callbacks.count = 0;
	memset(&db_callbacks.cbs, 0, sizeof (db_callbacks.cbs));
}

static void
_db_print_stderr(const char *format, va_list args)
{
    if (opt_debug_stderr < _db_level)
        return;
#if 0
    /* XXX regression! */
    if (debug_log == stderr)
        return;
#endif
    vfprintf(stderr, format, args);
}

static const char *
debugLogKid(void)
{
    if (KidIdentifier != 0) {
        static char buf[16];
        if (!*buf) // optimization: fill only once after KidIdentifier is set
            snprintf(buf, sizeof(buf), " kid%d", KidIdentifier);
        return buf;
    }

    return "";
}


void
#if STDC_HEADERS
_db_print(const char *path, int line, const char* function, const char *format,...)
{
#else
_db_print(va_alist)
     va_dcl
{
    const char *format = NULL;
#endif
    static char f[1024];
	static char buffer[BUFSIZ];
	static char __where[256];
    va_list args1;
    int i;

#if STDC_HEADERS	
	sprintf(__where, "%s(%d) %s", path,line,function);
#endif

#ifdef _SQUID_MSWIN_
    /* Multiple WIN32 threads may call this simultaneously */
    if (!dbg_mutex) {
	HMODULE krnl_lib = GetModuleHandle("Kernel32");
	BOOL(FAR WINAPI * InitializeCriticalSectionAndSpinCount)
	    (LPCRITICAL_SECTION, DWORD) = NULL;
	if (krnl_lib)
	    InitializeCriticalSectionAndSpinCount =
		GetProcAddress(krnl_lib,
		"InitializeCriticalSectionAndSpinCount");
	dbg_mutex = xcalloc(1, sizeof(CRITICAL_SECTION));

	if (InitializeCriticalSectionAndSpinCount) {
	    /* let multiprocessor systems EnterCriticalSection() fast */
	    if (!InitializeCriticalSectionAndSpinCount(dbg_mutex, 4000)) {
		if (debug_log) {
		    fprintf(debug_log, "FATAL: _db_print: can't initialize critical section\n");
		    fflush(debug_log);
		}
		fprintf(stderr, "FATAL: _db_print: can't initialize critical section\n");
		abort();
	    } else
		InitializeCriticalSection(dbg_mutex);
	}
    }
    EnterCriticalSection(dbg_mutex);
#endif
    /* give a chance to context-based debugging to print current context */
    if (!Ctx_Lock)
	ctx_print();
#if STDC_HEADERS
    va_start(args1, format);
#else
    format = va_arg(args1, const char *);
#endif

#if STDC_HEADERS
    /* "format" has no timestamp; "f" does! */
    snprintf(f, 1024, "%s%s| %s %s", debugLogTime(squid_curtime), debugLogKid(), __where, format);
#else
	snprintf(f, 1024, "%s%s| %s", debugLogTime(squid_curtime), debugLogKid(), format);
#endif

    _db_print_stderr(f, args1);
    va_end(args1);

    /* Send the string off to the individual section handlers */
    for (i = 0; i < db_callbacks.count; i++) {
#if STDC_HEADERS
        va_start(args1, format);
#else
        format = va_arg(args1, const char *);
#endif		
        if (db_callbacks.cbs[i].do_timestamp) {
		vsnprintf(buffer, BUFSIZ, f, args1);
		db_callbacks.cbs[i].cb("%s\n", buffer);
	} else {
		vsnprintf(buffer, BUFSIZ, format, args1);
		db_callbacks.cbs[i].cb("%s\n", buffer);
	}
        va_end(args1);
    }

#ifdef _SQUID_MSWIN_
    LeaveCriticalSection(dbg_mutex);
#endif
}

static void
debugArg(const char *arg)
{
    int s = 0;
    int l = 0;
    int i;
    if (!strncasecmp(arg, "ALL", 3)) {
	s = -1;
	arg += 4;
    } else {
	s = atoi(arg);
	while (*arg && *arg++ != ',');
    }
    l = atoi(arg);
    assert(s >= -1);
    assert(s < MAX_DEBUG_SECTIONS);
    if (l < 0)
	l = 0;
    if (l > 10)
	l = 10;
    if (s >= 0) {
	debugLevels[s] = l;
	return;
    }
    for (i = 0; i < MAX_DEBUG_SECTIONS; i++)
	debugLevels[i] = l;
}

void
_db_init(const char *options)
{
    int i;
    char *s = NULL;
    char *p;

    for (i = 0; i < MAX_DEBUG_SECTIONS; i++)
	debugLevels[i] = -1;

    if (_debug_options)
        xfree(_debug_options);
    _debug_options = NULL;
    if (options) {
	_debug_options = xstrdup(options);
	p = xstrdup(options);	/* XXX need this copy so strtok() can be done */
	for (s = strtok(p, w_space); s; s = strtok(NULL, w_space))
	    debugArg(s);
        xfree(p);
    }
}

static const char *
debugLogTime(time_t t)
{
    struct tm *tm;
    static char buf[128];
    static time_t last_t = 0;
    if (t != last_t) {
	tm = localtime(&t);
	strftime(buf, 127, "%Y/%m/%d %H:%M:%S", tm);
	last_t = t;
    }
    return buf;
}

void
xassert(const char *msg, const char *file, int line)
{
    debugs(0, 0, "assertion failed: %s:%d: \"%s\"", file, line, msg);
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

    if (!shutting_down)
	abort();
}

void  
_db_init_log(const char *logfile)
{
    _db_register_handler(_db_print_file, 1);
#if HAVE_SYSLOG
    _db_register_handler(_db_print_syslog, 0);
#endif
    debugOpenLog(logfile);
}   

static void DumpAsciiLine(const unsigned char *payload, int len, int offset)
{
    int i;
	int dbnum;
    int gap;
    const u_char *ch;

	for (dbnum = 0; dbnum < db_callbacks.count; dbnum++) 
	{
	    // offset
		db_callbacks.cbs[dbnum].cb("%05d   ", offset);

	    // hex
	    ch = payload;
	    for(i = 0; i < len; i++)
	    {
			db_callbacks.cbs[dbnum].cb("%02x ", *ch);
	        ch++;
	        // print extra space after 8th byte for visual aid
	        if (i == 7) db_callbacks.cbs[dbnum].cb("%s", " ");
	    }
	    
	    // print space to handle line less than 8 bytes
	    if (len < 8) db_callbacks.cbs[dbnum].cb("%s", " ");
	    
	    // fill hex gap with spaces if not full line 
	    if (len < 16) 
	    {
	        gap = 16 - len;
	        for (i = 0; i < gap; i++)
				db_callbacks.cbs[dbnum].cb("%s", "   ");
	    }
		
		db_callbacks.cbs[dbnum].cb("%s", "   ");

	    // ascii (if printable)
	    ch = payload;
	    for(i = 0; i < len; i++) 
	    {
	        if (isprint(*ch)) db_callbacks.cbs[dbnum].cb("%c", *ch); 
	        else              db_callbacks.cbs[dbnum].cb("%c", '.');
	        ch++;
	    }
		
		db_callbacks.cbs[dbnum].cb("%c", '\n');
	}
}

void DumpData(unsigned char *payload, int len)
{
	len = (len > 512) ? 512: len;

    int len_rem = len;
    int line_width = 16;// number of bytes per line
    int line_len;
    int offset = 0;     // zero-based offset counter
    const u_char *ch = payload;

    if (len <= 0)   return;
		
    // data fits on one line
    if (len <= line_width) 
    {
        DumpAsciiLine(ch, len, offset);
        return;
    }

    // data spans multiple lines
    for ( ;; ) 
    {
        // compute current line length
        line_len = line_width % len_rem;
        DumpAsciiLine(ch, line_len, offset);
        // compute total remaining 
        len_rem = len_rem - line_len;
        ch = ch + line_len; // shift pointer to remaining bytes to print
        offset = offset + line_width;

        if (len_rem <= line_width) 
        {
            // print last line and get out
            DumpAsciiLine(ch, len_rem, offset);
            break;
        }
    }

    return ;
}

static size_t BuildPrefixInit()
{
	// XXX: This must be kept in sync with the actual debug.cc location
	const char *ThisFileNameTail = "libsqdebug/debug.c";
	const char *file=__FILE__;
	// Disable heuristic if it does not work.
	if (!strstr(file, ThisFileNameTail))
		return 0;
	return strlen(file)-strlen(ThisFileNameTail);
}

const char* GSkipBuildPrefix(const char* path)
{
	size_t BuildPrefixLength = BuildPrefixInit();
	return path+BuildPrefixLength;
}
