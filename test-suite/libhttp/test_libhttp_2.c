#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "include/Array.h"
#include "include/Stack.h"
#include "include/Vector.h"
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
#include "libmem/String.h"
#include "libmem/MemStr.h"

#include "libcb/cbdata.h"

#include "libstat/StatHist.h"

#include "libsqinet/inet_legacy.h"
#include "libsqinet/sqinet.h"

#include "libhttp/HttpVersion.h"
#include "libhttp/HttpStatusLine.h"
#include "libhttp/HttpHeaderType.h"
#include "libhttp/HttpHeaderFieldStat.h"
#include "libhttp/HttpHeaderFieldInfo.h"
#include "libhttp/HttpHeaderEntry.h"
#include "libhttp/HttpHeader.h"
#include "libhttp/HttpHeaderStats.h"
#include "libhttp/HttpHeaderTools.h"
#include "libhttp/HttpHeaderMask.h"
#include "libhttp/HttpHeaderParse.h"

extern void httpHeaderRepack(HttpHeader * hdr);

static int
test2a(void)
{
	HttpHeader hdr;
	HttpHeaderPos pos = HttpHeaderInitPos;
	const HttpHeaderEntry *e;

	int ret;
	const char *hdrs = "Host: www.creative.net.au\r\nContent-type: text/html\r\nFoo: bar\r\n\r\n";
	const char *hdr_start = hdrs;
	const char *hdr_end = hdr_start + strlen(hdrs);

	printf("test1b: test parsing sample headers\n");
	httpHeaderInit(&hdr, hoRequest);
	ret = httpHeaderParse(&hdr, hdr_start, hdr_end);

	printf("  retval from parse: %d\n", ret);
	while ((e = httpHeaderGetEntry(&hdr, &pos))) {
		printf("  Parsed Header: %s: %s\n", strBuf(e->name), strBuf(e->value));
	}

	/* Delete teh content-type header */
	httpHeaderDelById(&hdr, HDR_CONTENT_TYPE);

	/* Add a new one */
	httpHeaderAddEntryStr(&hdr, HDR_CONTENT_TYPE, NULL, "text/plain");
	printf("After delete, after append\n");
	pos = HttpHeaderInitPos;
	while ((e = httpHeaderGetEntry(&hdr, &pos))) {
		printf("  Parsed Header: %s: %s\n", strBuf(e->name), strBuf(e->value));
	}
	httpHeaderRepack(&hdr);

	printf("After repack\n");
	pos = HttpHeaderInitPos;
	while ((e = httpHeaderGetEntry(&hdr, &pos))) {
		printf("  Parsed Header: %s: %s\n", strBuf(e->name), strBuf(e->value));
	}

	httpHeaderClean(&hdr);
	return 1;
}

int
main(int argc, const char *argv[])
{
	printf("%s: initializing\n", argv[0]);
	_db_init("ALL,99");
	_db_set_stderr_debug(99);
	memPoolInit();
	memBuffersInit();
	memStringInit();
	httpHeaderInitLibrary();
	test2a();
	exit(0);
}

