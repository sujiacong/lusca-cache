#include "../include/config.h"

#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include "../include/util.h"
#include "../libcore/valgrind.h"
#include "../libcore/varargs.h"
#include "../libcore/debug.h"

#include "signals.h"


void
squid_signal(int sig, SIGHDLR * func, int flags)
{
#if HAVE_SIGACTION
    struct sigaction sa;
    sa.sa_handler = func;
    sa.sa_flags = flags;
    sigemptyset(&sa.sa_mask);
    if (sigaction(sig, &sa, NULL) < 0)
        debugs(50, 0, "sigaction: sig=%d func=%p: %s", sig, func, xstrerror());
#else
#ifdef _SQUID_MSWIN_
/*
 * On Windows, only SIGINT, SIGILL, SIGFPE, SIGTERM, SIGBREAK, SIGABRT and SIGSEGV
 * signals are supported, so we must care of don't call signal() for other value.
 * The SIGILL, SIGSEGV, and SIGTERM signals are not generated under Windows. They
 * are defined only for ANSI compatibility, so both SIGSEGV and SIGBUS are emulated
 * with an Exception Handler.
 */
    switch (sig) {
    case SIGINT:
    case SIGILL:
    case SIGFPE:
    case SIGTERM:
    case SIGBREAK:
    case SIGABRT:
        break;
    case SIGSEGV:
        WIN32_ExceptionHandlerInit();
        break;
    case SIGBUS:
        WIN32_ExceptionHandlerInit();
        return;
        break;                  /* Not reached */
    default:
        return;
        break;                  /* Not reached */
    }
#endif /* _SQUID_MSWIN_ */
    signal(sig, func);
#endif
}

