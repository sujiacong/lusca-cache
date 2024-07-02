#include "include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <signal.h>
#if HAVE_ERRNO_H
#include <errno.h>
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
  
#include "include/Array.h"
#include "include/Stack.h"
#include "include/util.h"
#include "libcore/valgrind.h"
#include "libcore/varargs.h"
#include "libcore/debug.h"
#include "libcore/kb.h"
#include "libcore/gb.h"
#include "libcore/tools.h"

#include "libmem/MemPool.h"
#include "libmem/MemBufs.h"
#include "libmem/MemBuf.h"
  
#include "libcb/cbdata.h"

#include "libsqinet/inet_legacy.h"
#include "libsqinet/sqinet.h"

#include "libiapp/iapp_ssl.h"
#include "libiapp/fd_types.h"
#include "libiapp/comm_types.h"
#include "libiapp/comm.h"
#include "libiapp/pconn_hist.h"
#include "libiapp/signals.h"
#include "libiapp/mainloop.h"

#include "tunnel.h"

sqaddr_t dest;

static void
acceptSock(int sfd, void *d)
{
	int fd;
	sqaddr_t peer, me;

	do {
		bzero(&me, sizeof(me));
		bzero(&peer, sizeof(peer));
		sqinet_init(&me);
		sqinet_init(&peer);
		fd = comm_accept(sfd, &peer, &me);
		if (fd < 0) {
			sqinet_done(&me);
			sqinet_done(&peer);
			break;
		}
		debug(1, 2) ("acceptSock: FD %d: new socket!\n", fd);

		/* Create tunnel */
		sslStart(fd, &dest);
		sqinet_done(&me);
		sqinet_done(&peer);
	} while (1);
	/* register for another pass */
	commSetSelect(sfd, COMM_SELECT_READ, acceptSock, NULL, 0);
}

int
main(int argc, const char *argv[])
{
	int fd;
	sqaddr_t lcl4, lcl6;
	struct sockaddr_in s4, t;
	struct sockaddr_in6 s6;
	const char *host;
	short port;

	if (argc < 2) {
		printf("Usage: %s <host> <port>\n", argv[0]);
		exit(1);
	}

	iapp_init();
	squid_signal(SIGPIPE, SIG_IGN, SA_RESTART);

	_db_init("ALL,1");
	_db_set_stderr_debug(1);

	bzero(&s4.sin_addr, sizeof(s4.sin_addr));
	s4.sin_port = htons(8080);
#if !defined(_SQUID_LINUX_) && !defined(_SQUID_WIN32_)
	s4.sin_len = sizeof(struct sockaddr_in);
#endif
	sqinet_init(&lcl4);
	sqinet_set_v4_sockaddr(&lcl4, &s4);

	bzero(&s6, sizeof(s6));
	/* ANY_ADDR is all 0's here.. :) */
	s6.sin6_port = htons(8080);
#if !defined(_SQUID_LINUX_) && !defined(_SQUID_WIN32_)
	s6.sin6_len = sizeof(struct sockaddr_in6);
#endif
	sqinet_init(&lcl6);
	sqinet_set_v6_sockaddr(&lcl6, &s6);

	host = argv[1];
	port = atoi(argv[2]);

	debug(1, 1) ("main: forwarding to %s:%d\n", host, port);

	bzero(&t, sizeof(t));
	t.sin_port = htons(port);
	t.sin_family = AF_INET;
	safe_inet_addr(host, &t.sin_addr);
	sqinet_init(&dest);
	sqinet_set_v4_sockaddr(&dest, &t);

	fd = comm_open6(SOCK_STREAM, IPPROTO_TCP, &lcl6, COMM_NONBLOCKING, COMM_TOS_DEFAULT, "HTTP Socket v6");
	fd = comm_open6(SOCK_STREAM, IPPROTO_TCP, &lcl4, COMM_NONBLOCKING, COMM_TOS_DEFAULT, "HTTP Socket v4");

	assert(fd > 0);
	comm_listen(fd);
	commSetSelect(fd, COMM_SELECT_READ, acceptSock, NULL, 0);

	while (1) {
		iapp_runonce(60000);
	}

	exit(0);
}

