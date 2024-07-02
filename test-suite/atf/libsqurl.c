
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

#include "include/util.h"

#include "libsqurl/domain.h"
#include "libsqurl/proto.h"
#include "libsqurl/url.h"

#include "core.h"

ATF_TC(libsqurl_domain_1);
ATF_TC_HEAD(libsqurl_domain_1, tc)
{
	atf_tc_set_md_var(tc, "descr", "test matchDomainName()");
}

ATF_TC_BODY(libsqurl_domain_1, tc)
{
	test_core_init();

	ATF_REQUIRE(0 == matchDomainName("foo.com", "foo.com"));
	ATF_REQUIRE(0 == matchDomainName(".foo.com", "foo.com"));
	ATF_REQUIRE(0 == matchDomainName("foo.com", ".foo.com"));
	ATF_REQUIRE(0 == matchDomainName(".foo.com", ".foo.com"));
	ATF_REQUIRE(0 == matchDomainName("x.foo.com", ".foo.com"));
	ATF_REQUIRE(0 != matchDomainName("x.foo.com", "foo.com"));
	ATF_REQUIRE(0 != matchDomainName("foo.com", "x.foo.com"));
	ATF_REQUIRE(0 != matchDomainName("bar.com", "foo.com"));
	ATF_REQUIRE(0 != matchDomainName(".bar.com", "foo.com"));
	ATF_REQUIRE(0 != matchDomainName(".bar.com", ".foo.com"));
	ATF_REQUIRE(0 != matchDomainName("bar.com", ".foo.com"));
	ATF_REQUIRE(0 < matchDomainName("zzz.com", "foo.com"));
	ATF_REQUIRE(0 > matchDomainName("aaa.com", "foo.com"));
	ATF_REQUIRE(0 == matchDomainName("FOO.com", "foo.COM"));
	ATF_REQUIRE(0 < matchDomainName("bfoo.com", "afoo.com"));
	ATF_REQUIRE(0 > matchDomainName("afoo.com", "bfoo.com"));
	ATF_REQUIRE(0 < matchDomainName("x-foo.com", ".foo.com"));
}

ATF_TC(libsqurl_urlmakehttpcanonical_1);
ATF_TC_HEAD(libsqurl_urlmakehttpcanonical_1, tc)
{
	atf_tc_set_md_var(tc, "descr", "test urlMakeHttpCanonical()");
}
ATF_TC_BODY(libsqurl_urlmakehttpcanonical_1, tc)
{
}

ATF_TC(libsqurl_urlmakehttpcanonical_2);
ATF_TC_HEAD(libsqurl_urlmakehttpcanonical_2, tc)
{
	atf_tc_set_md_var(tc, "descr", "test urlMakeHttpCanonical2()");
}
ATF_TC_BODY(libsqurl_urlmakehttpcanonical_2, tc)
{
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, libsqurl_domain_1);
	ATF_TP_ADD_TC(tp, libsqurl_urlmakehttpcanonical_1);
	ATF_TP_ADD_TC(tp, libsqurl_urlmakehttpcanonical_2);
	return atf_no_error();
}

