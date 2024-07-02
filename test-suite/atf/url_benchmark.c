#include "include/config.h"

#include <atf-c.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>

#include "include/util.h"

#include "libsqurl/defines.h"
#include "libsqurl/domain.h"
#include "libsqurl/proto.h"
#include "libsqurl/url.h"

/*
 * This is designed to do some very, very basic benchmarking
 * of the URL assembly code.
 */

typedef int URLFUNC(char *, protocol_t, const char *, const char *, int, const char *, int urlpath_len);

long
do_benchmark(int nloop, URLFUNC *f, protocol_t proto, const char *login, const char *host, int port, const char *urlpath)
{
	int urlpath_len;
	char urlbuf[MAX_URL];
	int i;
	struct timeval ts, te;
	long sd, ud;

	urlpath_len = strlen(urlpath);

	gettimeofday(&ts, NULL);
	for (i = 0; i < nloop; i++) {
		urlbuf[0] = '\0';
		(void) f(urlbuf, proto, login, host, port, urlpath, urlpath_len);
	}
	gettimeofday(&te, NULL);

	sd = te.tv_sec - ts.tv_sec;
	ud = te.tv_usec - ts.tv_usec;
	if (sd > 0)
		ud = ud + (sd * 1000000);

	return ud;
}

void
do_run(int iloop, int nloop, const char *tag, URLFUNC *f, protocol_t proto, const char *login, const char *host, int port, const char *urlpath)
{
	int i;
	long l;

	printf(": %s\n", tag);
	for (i = 0; i < iloop; i++) {
		l = do_benchmark(nloop, f, proto, login, host, port, urlpath);
		printf("  %d: %ld msec; %.3f usec per request\n", i, l / 1000, (float) l / (float) nloop);
	}
	printf("--\n");
}

int
main(int argc, const char *argv[])
{
	int i;

	/* old/new, defaults */
	do_run(10, 100000, "old", urlMakeHttpCanonical, PROTO_HTTP,
	    "", "www.creative.net.au", 80, "/test.html");
	do_run(10, 100000, "new", urlMakeHttpCanonical2, PROTO_HTTP,
	    "", "www.creative.net.au", 80, "/test.html");

	/* old/new, non-standard port (triggers another printf call) */
	do_run(10, 100000, "old, port 81", urlMakeHttpCanonical, PROTO_HTTP,
	    "", "www.creative.net.au", 81, "/test.html");
	do_run(10, 100000, "new, port 81", urlMakeHttpCanonical2, PROTO_HTTP,
	    "", "www.creative.net.au", 81, "/test.html");

	/* old/new, login, default port */
	do_run(10, 100000, "old, port 80, login", urlMakeHttpCanonical, PROTO_HTTP,
	    "username", "www.creative.net.au", 80, "/test.html");
	do_run(10, 100000, "new, port 80, login", urlMakeHttpCanonical2, PROTO_HTTP,
	    "username", "www.creative.net.au", 80, "/test.html");

	/* old/new, login, non-standard port */
	do_run(10, 100000, "old, port 81, login", urlMakeHttpCanonical, PROTO_HTTP,
	    "username", "www.creative.net.au", 81, "/test.html");
	do_run(10, 100000, "new, port 81, login", urlMakeHttpCanonical2, PROTO_HTTP,
	    "username", "www.creative.net.au", 81, "/test.html");
}

