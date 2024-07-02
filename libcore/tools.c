#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#if HAVE_STRING_H
#include <string.h>
#endif

#if HAVE_BACKTRACE_SYMBOLS_FD
#include <execinfo.h>
#endif

#include "varargs.h"
#include "tools.h"

double current_dtime;
time_t squid_curtime = 0;
struct timeval current_time;

const char *w_space = " \t\n\r";

static void libcore_internal_fatalf(const char *fmt, va_list args);

static FATALF_FUNC *libcore_fatalf_func = libcore_internal_fatalf;

double
toMB(size_t size)
{
    return ((double) size) / MB;
}
 
size_t
toKB(size_t size)
{
    return (size + 1024 - 1) / 1024;
}

time_t
getCurrentTime(void)
{
#if GETTIMEOFDAY_NO_TZP
    gettimeofday(&current_time);
#else
    gettimeofday(&current_time, NULL);
#endif
    current_dtime = (double) current_time.tv_sec +
        (double) current_time.tv_usec / 1000000.0;
    return squid_curtime = current_time.tv_sec;
}

void
libcore_internal_fatalf(const char *fmt, va_list args)
{
    vfprintf(stderr, fmt, args);
    abort();
}

void
libcore_fatalf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    libcore_fatalf_func(fmt, args);
    va_end(args);
}

void
libcore_set_fatalf(FATALF_FUNC *f)
{
	libcore_fatalf_func = f;
}

/*
 * xusleep, as usleep but accepts longer pauses
 */
int
xusleep(unsigned int usec)
{
    /* XXX emulation of usleep() */
    struct timeval sl;
    sl.tv_sec = usec / 1000000;
    sl.tv_usec = usec % 1000000;
    return select(0, NULL, NULL, NULL, &sl);
}  


int
stringHasWhitespace(const char *s)
{
    return strpbrk(s, w_space) != NULL;
}

double
doubleAverage(double cur, double new, int N, int max)
{
    if (N > max)
        N = max;
    return (cur * (N - 1.0) + new) / N;
} 
 
int
intAverage(int cur, int new, int n, int max)
{ 
    if (n > max)
        n = max;
    return (cur * (n - 1) + new) / n;
}

void
doBacktrace(void)
{
        fprintf(stderr, "backtrace:\n"); fflush(stderr);
#if HAVE_BACKTRACE_SYMBOLS_FD
    {   
        static void *(callarray[8192]);
        int n;
        n = backtrace(callarray, 8192);
        backtrace_symbols_fd(callarray, n, fileno(stderr));
    }
#endif
#ifdef _SQUID_HPUX_
    {   
        extern void U_STACK_TRACE(void);        /* link with -lcl */
        U_STACK_TRACE();
        fflush(stderr);
    }
#endif /* _SQUID_HPUX_ */
#ifdef _SQUID_SOLARIS_
    {                           /* get ftp://opcom.sun.ca/pub/tars/opcom_stack.tar.gz and */
        extern void opcom_stack_trace(void);    /* link with -lopcom_stack */
	/* XXX does this dump to stdout or stderr? */
        opcom_stack_trace();
        fflush(stdout);
    }
#endif /* _SQUID_SOLARIS_ */
}

int     
percent(int a, int b)
{       
    return b ? ((int) (100.0 * a / b + 0.5)) : 0;
}   
    
double 
dpercent(double a, double b)
{   
    return b ? (100.0 * a / b) : 0.0;
}   

double
uint64_percent(u_int64_t a, u_int64_t b)
{
    return (double) b ? ((double) (100.0 * (double) a / (double) b + 0.5)) : 0.0;
}
