#ifndef	__LIBIAPP_SIGNALS_H__
#define	__LIBIAPP_SIGNALS_H__

typedef void SIGHDLR(int sig);
extern void squid_signal(int sig, SIGHDLR *, int flags);



#endif
