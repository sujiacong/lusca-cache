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

extern int hh_check_content_length(HttpHeader *hdr, const char *val, int vlen);

static int
test_hh_content_length(HttpHeader *hdr, const char *str)
{
	int r;

	/* XXX remember; this may delete items from the header entry array! */
	r = hh_check_content_length(hdr, str, strlen(str));
	return r;
}

static int
test1c()
{
	HttpHeader hdr;
	int ret;
	const char *hdrs = "Host: www.creative.net.au\r\nContent-Length: 12345\r\nContent-type: text/html\r\nFoo: bar\r\n\r\n";
	const char *hdr_start = hdrs;
	const char *hdr_end = hdr_start + strlen(hdrs);

	printf("test1c: test hh_check_content_length\n");

	httpHeaderInit(&hdr, hoRequest);

	printf("test1c: hh_check_content_length: 12345 = %d\n", test_hh_content_length(&hdr, "12345"));
	printf("test1c: hh_check_content_length: 123b5 = %d\n", test_hh_content_length(&hdr, "123b5"));
	printf("test1c: hh_check_content_length: b1234 = %d\n", test_hh_content_length(&hdr, "b1234"));
	printf("test1c: hh_check_content_length: abcde = %d\n", test_hh_content_length(&hdr, "abcde"));

	/* now check duplicates */
	ret = httpHeaderParse(&hdr, hdr_start, hdr_end);
	printf("test1c: httpHeaderParse: Returned %d\n", ret);

	printf("test1c: hh_check_content_length: 12345 = %d\n", test_hh_content_length(&hdr, "12345"));
	printf("test1c: hh_check_content_length: 123b5 = %d\n", test_hh_content_length(&hdr, "123b5"));
	printf("test1c: hh_check_content_length: b1234 = %d\n", test_hh_content_length(&hdr, "b1234"));
	printf("test1c: hh_check_content_length: 12344 = %d\n", test_hh_content_length(&hdr, "12344"));
	printf("test1c: hh_check_content_length: 12346 = %d\n", test_hh_content_length(&hdr, "12346"));
	printf("test1c: hh_check_content_length: 12344 = %d\n", test_hh_content_length(&hdr, "12344"));

	printf("test1c: hh_check_content_length: setting httpConfig_relaxed_parser to 1 (ok)\n");
	httpConfig_relaxed_parser = 1;
	printf("test1c: hh_check_content_length: 12345 = %d\n", test_hh_content_length(&hdr, "12345"));
	printf("test1c: hh_check_content_length: 123b5 = %d\n", test_hh_content_length(&hdr, "123b5"));
	printf("test1c: hh_check_content_length: b1234 = %d\n", test_hh_content_length(&hdr, "b1234"));

	printf("test1c: hh_check_content_length: 12344 = %d\n", test_hh_content_length(&hdr, "12344"));
	/* this one should result in the deletion of the "12345" entry from the original request parse */
	printf("test1c: hh_check_content_length: 12346 = %d\n", test_hh_content_length(&hdr, "12346"));
	printf("test1c: hh_check_content_length: 12344 = %d\n", test_hh_content_length(&hdr, "12344"));

	/* Clean up */
	httpHeaderReset(&hdr);
	httpHeaderClean(&hdr);
	
	return 1;
}

static int
test1a(void)
{
	HttpHeader hdr;

	printf("test1a: test initialisation/destruction/reset\n");
	httpHeaderInit(&hdr, hoRequest);
	httpHeaderReset(&hdr);
	httpHeaderClean(&hdr);
	return 1;
}

static int
test1b(void)
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
	test1a();
	test1b();
	test1c();
	exit(0);
}

