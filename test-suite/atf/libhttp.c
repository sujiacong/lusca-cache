
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

#include "core.h"

struct _http_repack_list {
	const char *n;
	const char *v;
	http_hdr_type t;
	int valid;
};
void
http_hdrs_assemble(char *hdrs, struct _http_repack_list *r)
{
	int i;

	hdrs[0] = '\0';
	for (i = 0; r[i].n != NULL; i++) {
		strcat(hdrs, r[i].n);
		strcat(hdrs, ": ");
		strcat(hdrs, r[i].v);
		strcat(hdrs, "\r\n");
	}
	strcat(hdrs, "\r\n");
}

void
http_hdrs_repack_del(HttpHeader *hdrs, struct _http_repack_list *r, int ti)
{
	/* Remove the Content-Length header */
	if (r[ti].t == HDR_OTHER)
		httpHeaderDelByName(hdrs, r[ti].n);
	else
		httpHeaderDelById(hdrs, r[ti].t);
	r[ti].valid = 0;
}

void
http_hdrs_check(HttpHeader *hdrs, struct _http_repack_list *r)
{
	volatile int i, j;
	HttpHeaderPos pos;
	HttpHeaderEntry *e;

	pos = HttpHeaderInitPos;
	for (i = 0; r[i].n != NULL; i++) {
		if (r[i].valid == 0)
			continue;
		e = httpHeaderGetEntry(hdrs, &pos);
		ATF_REQUIRE(e != NULL);
		ATF_REQUIRE(e->id == r[i].t);
		ATF_REQUIRE(strCmp(e->value, r[i].v) == 0);
		if (e->id == HDR_OTHER)
			ATF_REQUIRE(strCmp(e->name, r[i].n) == 0);
	}
}

void
http_hdrs_check_again(HttpHeader *hdrs, struct _http_repack_list *r)
{
	http_hdrs_check(hdrs, r);
	httpHeaderRepack(hdrs);
	http_hdrs_check(hdrs, r);
}


/* ** */

/* XXX should be in an include file from libhttp! */
typedef enum {
        PR_NONE,
        PR_ERROR,
        PR_IGNORE,
        PR_WARN,
        PR_OK
} parse_retval_t;
extern parse_retval_t hh_check_content_length(HttpHeader *hdr, const char *val, int vlen);

static int
test_core_parse_header(HttpHeader *hdr, const char *hdrs)
{
        const char *hdr_start = hdrs;
        const char *hdr_end = hdr_start + strlen(hdrs);

        httpHeaderInit(hdr, hoRequest);
        return httpHeaderParse(hdr, hdr_start, hdr_end);
}

static void
libhttp_test_parser(const char *str, int ret)
{
	HttpHeader hdr;

	ATF_CHECK_EQ(test_core_parse_header(&hdr, str), ret);
	httpHeaderClean(&hdr);
}

static void
libhttp_test_content_length_parser(const char *str, const char *clength)
{
	HttpHeader hdr;
	HttpHeaderEntry *e;

	ATF_CHECK_EQ(test_core_parse_header(&hdr, str), 1);

	/* Verify the content-length header is what it should be */
	e = httpHeaderFindEntry(&hdr, HDR_CONTENT_LENGTH);
	ATF_REQUIRE(e != NULL);
	ATF_REQUIRE(strNCmp(e->value, clength, strlen(clength)) == 0);

	httpHeaderClean(&hdr);
}

static parse_retval_t
test_http_content_length(HttpHeader *hdr, const char *str)
{
	int r;

	/* XXX remember; this may delete items from the header entry array! */
	r = hh_check_content_length(hdr, str, strlen(str));
	return r;
}

/* ** */

/*
 * This only deletes the item at the given location!
 */
static void
http_hdrs_parse(HttpHeader *hdr, struct _http_repack_list *r)
{
	char hdrs[1024];

	/* Assemble a "joined" header set for parsing */
	http_hdrs_assemble(hdrs, r);

	/* Parse header set */
	ATF_REQUIRE(test_core_parse_header(hdr, hdrs) == 1);
}


/* *** */

ATF_TC(libhttp_parse_1);
ATF_TC_HEAD(libhttp_parse_1, tc)
{
	atf_tc_set_md_var(tc, "descr", "libhttp_parse_1");
}

ATF_TC_BODY(libhttp_parse_1, tc)
{
	test_core_init();
	httpHeaderInitLibrary();
	libhttp_test_parser("Host: www.creative.net.au\r\nContent-type: text/html\r\nFoo: bar\r\n\r\n", 1);
}

ATF_TC(libhttp_parse_2);
ATF_TC_HEAD(libhttp_parse_2, tc)
{
	atf_tc_set_md_var(tc, "descr", "content-length header parsing");
}

ATF_TC_BODY(libhttp_parse_2, tc)
{
	test_core_init();
	httpHeaderInitLibrary();
	libhttp_test_parser("Host: www.creative.net.au\r\nContent-Length: 12345\r\nContent-type: text/html\r\nFoo: bar\r\n\r\n", 1);
}

/* ** */

ATF_TC(libhttp_parse_3);
ATF_TC_HEAD(libhttp_parse_3, tc)
{
	atf_tc_set_md_var(tc, "descr", "content-length header parsing - failure");
}

ATF_TC_BODY(libhttp_parse_3, tc)
{
	test_core_init();
	httpHeaderInitLibrary();
	libhttp_test_parser("Host: www.creative.net.au\r\nContent-Length: b12345\r\nContent-type: text/html\r\nFoo: bar\r\n\r\n", 0);
}

/* *** */

ATF_TC(libhttp_parse_4);
ATF_TC_HEAD(libhttp_parse_4, tc)
{
	atf_tc_set_md_var(tc, "descr", "content-length header parsing - two conflicting Content-Length headers; failure");
}

ATF_TC_BODY(libhttp_parse_4, tc)
{
	test_core_init();
	httpHeaderInitLibrary();
	libhttp_test_parser("Host: www.creative.net.au\r\nContent-Length: 12345\r\nContent-type: text/html\r\nFoo: bar\r\nContent-Length: 23456\r\n", 0);
}

/* *** */

ATF_TC(libhttp_parse_content_length_1);

ATF_TC_HEAD(libhttp_parse_content_length_1, tc)
{
	atf_tc_set_md_var(tc, "descr", "libhttp_parse_content_length_1");
}

ATF_TC_BODY(libhttp_parse_content_length_1, tc)
{
	HttpHeader hdr;

	test_core_init();
        httpHeaderInitLibrary();
	httpHeaderInit(&hdr, hoRequest);

	ATF_REQUIRE(test_http_content_length(&hdr, "12345") == PR_OK);
	ATF_REQUIRE(test_http_content_length(&hdr, "123b5") == PR_OK);
	ATF_REQUIRE(test_http_content_length(&hdr, "b1234") == PR_ERROR);
	ATF_REQUIRE(test_http_content_length(&hdr, "abcde") == PR_ERROR);
	ATF_REQUIRE(test_http_content_length(&hdr, "4790023270") == PR_OK);

	/* Clean up */
	httpHeaderClean(&hdr);
}

ATF_TC(libhttp_parse_content_length_2);
ATF_TC_HEAD(libhttp_parse_content_length_2, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check that duplicate Content-Length headers are "
	    "correctly replaced with the relaxed HTTP parser enabled");
}
ATF_TC_BODY(libhttp_parse_content_length_2, tc)
{
	test_core_init();
	httpHeaderInitLibrary();
	httpConfig_relaxed_parser = 1;
	libhttp_test_content_length_parser("Content-Length: 12345\r\nContent-Length: 23456\r\n", "23456");
	libhttp_test_content_length_parser("Content-Length: 23456\r\nContent-Length: 12345\r\n", "23456");
	libhttp_test_content_length_parser("Content-Length: 23456\r\nContent-Length: 12345\r\nContent-Length: 23456\r\n", "23456");
	libhttp_test_content_length_parser("Content-Length: 4790023270\r\n", "4790023270");
}

ATF_TC(libhttp_parser_other_whitespace_1);
ATF_TC_HEAD(libhttp_parser_other_whitespace_1, tc)
{
	atf_tc_set_md_var(tc, "descr", "Headers must not have whitespace in the field names");
}
ATF_TC_BODY(libhttp_parser_other_whitespace_1, tc)
{
	test_core_init();
	httpHeaderInitLibrary();
	libhttp_test_parser("Fo o: bar\r\n", 0);
}

ATF_TC(libhttp_repack_1);
ATF_TC_HEAD(libhttp_repack_1, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check that httpHeaderRepack() doesn't mangle the header list");
}
ATF_TC_BODY(libhttp_repack_1, tc)
{
	HttpHeader hdr;

	struct _http_repack_list r[] =
		{ { "Content-Type", "text/html; charset=UTF=8", HDR_CONTENT_TYPE, 1 },
		  { "Foo", "bar", HDR_OTHER, 1 },
		  { "Content-Length", "12345", HDR_CONTENT_LENGTH, 1 },
		  { "Accept", "*/*", HDR_ACCEPT, 1 },
		  { "Host", "www.creative.net.au", HDR_HOST, 1 },
		  { "Set-Cookie", "@tjackson-11758> use some dyndns-ish stuff for it, or pay for a static IP from the carrier?", HDR_SET_COOKIE, 1 },
		  { "Date", "Thu, 10 Jun 2010 15:08:25 GMT", HDR_DATE, 1 },
		  { "Server", "Server: Apache/2.2.3 (Debian)", HDR_SERVER, 1 },
		  { "Accept-Ranges", "bytes", HDR_ACCEPT_RANGES, 1 },
		  { "Connection", "close", HDR_CONNECTION, 1 },
		  { "Age", "128", HDR_AGE, 1 },
		  { "X-Origin-Date", "Thu, 10 Jun 2010 15:00:48 GMT", HDR_OTHER, 1 },
		  { "Via", "1.0 mirror1.jp.cacheboy.net:80 (LUSCA/Lusca_HEAD)", HDR_VIA, 1 },
		  { "X-Cache", "HIT from mirror1.jp.cacheboy.net", HDR_X_CACHE, 1 },
		  { "X-Cache-Age", "128", HDR_OTHER, 1 },
		  { "X-Slashdot", "bender - she'll be right, mate!", HDR_OTHER, 1},
		  { "X-Slashdot-2", "another bender - she'll be right, mate!", HDR_OTHER, 1},

		  { NULL, NULL, HDR_UNKNOWN, 0 }};

	test_core_init();
	httpHeaderInitLibrary();
	http_hdrs_parse(&hdr, r);

	/* Ensure the headers are still as expected */
	http_hdrs_check_again(&hdr, r);
	http_hdrs_repack_del(&hdr, r, 2);
	http_hdrs_check_again(&hdr, r);
	http_hdrs_repack_del(&hdr, r, 3);
	http_hdrs_check_again(&hdr, r);
	http_hdrs_repack_del(&hdr, r, 15);
	http_hdrs_check_again(&hdr, r);

#if 0
	httpHeaderInsertTime(&hdr, 14, HDR_DATE, 123456789);
#endif
	httpHeaderClean(&hdr);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, libhttp_parse_1);
	ATF_TP_ADD_TC(tp, libhttp_parse_2);
	ATF_TP_ADD_TC(tp, libhttp_parse_3);
	ATF_TP_ADD_TC(tp, libhttp_parse_4);
	ATF_TP_ADD_TC(tp, libhttp_parse_content_length_1);
	ATF_TP_ADD_TC(tp, libhttp_parse_content_length_2);
	ATF_TP_ADD_TC(tp, libhttp_parser_other_whitespace_1);
	ATF_TP_ADD_TC(tp, libhttp_repack_1);
	return atf_no_error();
}

