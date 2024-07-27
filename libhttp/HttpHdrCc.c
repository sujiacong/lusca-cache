
/*
 * $Id: HttpHdrCc.c 14599 2010-04-14 03:49:57Z adrian.chadd $
 *
 * DEBUG: section 65    HTTP Cache Control Header
 * AUTHOR: Alex Rousskov
 *
 * SQUID Web Proxy Cache          http://www.squid-cache.org/
 * ----------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from
 *  the Internet community; see the CONTRIBUTORS file for full
 *  details.   Many organizations have provided support for Squid's
 *  development; see the SPONSORS file for full details.  Squid is
 *  Copyrighted (C) 2001 by the Regents of the University of
 *  California; see the COPYRIGHT file for full details.  Squid
 *  incorporates software developed and/or copyrighted by other
 *  sources; see the CREDITS file for full details.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 *
 */

#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "../include/Array.h"
#include "../include/Stack.h"
#include "../include/Vector.h"
#include "../include/util.h"
#include "../libcore/valgrind.h"
#include "../libcore/varargs.h"
#include "../libcore/debug.h"
#include "../libcore/kb.h"
#include "../libcore/gb.h"
#include "../libcore/tools.h"

#include "../libmem/MemPool.h"
#include "../libmem/MemBufs.h"
#include "../libmem/MemBuf.h"
#include "../libmem/String.h"
#include "../libmem/StrList.h"

#include "../libcb/cbdata.h"

#include "../libstat/StatHist.h"

#include "HttpVersion.h"
#include "HttpStatusLine.h"
#include "HttpHeaderType.h"
#include "HttpHeaderFieldStat.h"
#include "HttpHeaderFieldInfo.h"
#include "HttpHeaderEntry.h"
#include "HttpHeader.h"
#include "HttpHeaderStats.h"
#include "HttpHeaderTools.h"
#include "HttpHeaderMask.h"
#include "HttpHeaderVars.h"
#include "HttpHeaderList.h"
#include "HttpHeaderParse.h"
#include "HttpHeaderGet.h"
#include "HttpHdrCc.h"

/* this table is used for parsing cache control header */
const HttpHeaderFieldAttrs CcAttrs[CC_ENUM_END] =
{
    {"public", CC_PUBLIC},
    {"private", CC_PRIVATE},
    {"no-cache", CC_NO_CACHE},
    {"no-store", CC_NO_STORE},
    {"no-transform", CC_NO_TRANSFORM},
    {"must-revalidate", CC_MUST_REVALIDATE},
    {"proxy-revalidate", CC_PROXY_REVALIDATE},
    {"only-if-cached", CC_ONLY_IF_CACHED},
    {"max-age", CC_MAX_AGE},
    {"s-maxage", CC_S_MAXAGE},
    {"max-stale", CC_MAX_STALE},
    {"stale-while-revalidate", CC_STALE_WHILE_REVALIDATE},
    {"stale-if-error", CC_STALE_IF_ERROR},
    {"Other,", CC_OTHER}	/* ',' will protect from matches */
};
HttpHeaderFieldInfo *CcFieldsInfo = NULL;

/* local prototypes */
static int httpHdrCcParseInit(HttpHdrCc * cc, const String * str);

static MemPool * pool_http_hdr_cc = NULL;

/* module initialization */

void
httpHdrCcInitModule(void)
{
    CcFieldsInfo = httpHeaderBuildFieldsInfo(CcAttrs, CC_ENUM_END);
    pool_http_hdr_cc = memPoolCreate("HttpHdrCc", sizeof(HttpHdrCc));
}

void
httpHdrCcCleanModule(void)
{
    httpHeaderDestroyFieldsInfo(CcFieldsInfo, CC_ENUM_END);
    CcFieldsInfo = NULL;
}

/* implementation */

HttpHdrCc *
httpHdrCcCreate(void)
{
    HttpHdrCc *cc = memPoolAlloc(pool_http_hdr_cc);
    cc->max_age = cc->s_maxage = cc->max_stale = cc->stale_if_error - 1;
    return cc;
}

/* creates an cc object from a 0-terminating string */
HttpHdrCc *
httpHdrCcParseCreate(const String * str)
{
    HttpHdrCc *cc = httpHdrCcCreate();
    if (!httpHdrCcParseInit(cc, str)) {
	httpHdrCcDestroy(cc);
	cc = NULL;
    }
    return cc;
}

/* parses a 0-terminating string and inits cc */
static int
httpHdrCcParseInit(HttpHdrCc * cc, const String * str)
{
    const char *item;
    const char *p;		/* '=' parameter */
    const char *pos = NULL;
    int type;
    int ilen;
    int nlen;
    assert(cc && str);

    /* iterate through comma separated list */
    while (strListGetItem(str, ',', &item, &ilen, &pos)) {
	/* isolate directive name */
	if ((p = memchr(item, '=', ilen)))
	    nlen = p++ - item;
	else
	    nlen = ilen;
	/* find type */
	type = httpHeaderIdByName(item, nlen,
	    CcFieldsInfo, CC_ENUM_END);
	if (type < 0) {
	    debugs(65, 2, "hdr cc: unknown cache-directive: near '%s' in '%.*s'", item, strLen2(*str), strBuf2(*str));
	    type = CC_OTHER;
	}
	if (EBIT_TEST(cc->mask, type)) {
	    if (type != CC_OTHER)
		debugs(65, 2, "hdr cc: ignoring duplicate cache-directive: near '%s' in '%.*s'", item, strLen2(*str), strBuf2(*str));
	    CcFieldsInfo[type].stat.repCount++;
	    continue;
	}
	/* update mask */
	EBIT_SET(cc->mask, type);
	/* post-processing special cases */
	switch (type) {
	case CC_MAX_AGE:
	    if (!p || !httpHeaderParseInt(p, &cc->max_age)) {
		debugs(65, 2, "httpHdrCcParseInit: invalid max-age specs near '%s'", item);
		cc->max_age = -1;
		EBIT_CLR(cc->mask, type);
	    }
	    break;
	case CC_S_MAXAGE:
	    if (!p || !httpHeaderParseInt(p, &cc->s_maxage)) {
		debugs(65, 2, "httpHdrCcParseInit: invalid s-maxage specs near '%s'", item);
		cc->s_maxage = -1;
		EBIT_CLR(cc->mask, type);
	    }
	    break;
	case CC_MAX_STALE:
	    if (!p) {
		debugs(65, 3, "httpHdrCcParseInit: max-stale directive is valid without value");
		cc->max_stale = -1;
	    } else if (!httpHeaderParseInt(p, &cc->max_stale)) {
		debugs(65, 2, "httpHdrCcParseInit: invalid max-stale specs near '%s'", item);
		cc->max_stale = -1;
		EBIT_CLR(cc->mask, type);
	    }
	    break;
	case CC_STALE_WHILE_REVALIDATE:
	    if (!p || !httpHeaderParseInt(p, &cc->stale_while_revalidate)) {
		debugs(65, 2, "httpHdrCcParseInit: invalid stale-while-revalidate specs near '%s'", item);
		cc->stale_while_revalidate = -1;
		EBIT_CLR(cc->mask, type);
	    }
	    break;
	case CC_STALE_IF_ERROR:
	    if (!p || !httpHeaderParseInt(p, &cc->stale_if_error)) {
		debugs(65, 2, "httpHdrCcParseInit: invalid stale-if-error specs near '%s'", item);
		cc->stale_if_error = -1;
		EBIT_CLR(cc->mask, type);
	    }
	    break;
	case CC_OTHER:
	    if (strLen(cc->other))
		strCat(cc->other, ", ");
	    stringAppend(&cc->other, item, ilen);
	    break;
	default:
	    /* note that we ignore most of '=' specs (RFC-VIOLATION) */
	    break;
	}
    }
    return cc->mask != 0;
}

void
httpHdrCcDestroy(HttpHdrCc * cc)
{
    assert(cc);
    if (strIsNotNull(cc->other))
	stringClean(&cc->other);
    memPoolFree(pool_http_hdr_cc, cc);
}

HttpHdrCc *
httpHdrCcDup(const HttpHdrCc * cc)
{
    HttpHdrCc *dup;
    assert(cc);
    dup = httpHdrCcCreate();
    dup->mask = cc->mask;
    dup->max_age = cc->max_age;
    dup->s_maxage = cc->s_maxage;
    dup->max_stale = cc->max_stale;
    return dup;
}

void
httpHdrCcJoinWith(HttpHdrCc * cc, const HttpHdrCc * new_cc)
{
    assert(cc && new_cc);
    if (cc->max_age < 0)
	cc->max_age = new_cc->max_age;
    if (cc->s_maxage < 0)
	cc->s_maxage = new_cc->s_maxage;
    if (cc->max_stale < 0)
	cc->max_stale = new_cc->max_stale;
    cc->mask |= new_cc->mask;
}

/* negative max_age will clean old max_Age setting */
void
httpHdrCcSetMaxAge(HttpHdrCc * cc, int max_age)
{
    assert(cc);
    cc->max_age = max_age;
    if (max_age >= 0)
	EBIT_SET(cc->mask, CC_MAX_AGE);
    else
	EBIT_CLR(cc->mask, CC_MAX_AGE);
}

/* negative s_maxage will clean old s-maxage setting */
void
httpHdrCcSetSMaxAge(HttpHdrCc * cc, int s_maxage)
{
    assert(cc);
    cc->s_maxage = s_maxage;
    if (s_maxage >= 0)
	EBIT_SET(cc->mask, CC_S_MAXAGE);
    else
	EBIT_CLR(cc->mask, CC_S_MAXAGE);
}

void
httpHdrCcUpdateStats(const HttpHdrCc * cc, StatHist * hist)
{
    http_hdr_cc_type c;
    assert(cc);
    for (c = 0; c < CC_ENUM_END; c++)
	if (EBIT_TEST(cc->mask, c))
	    statHistCount(hist, c);
}

HttpHdrCc *
httpHeaderGetCc(const HttpHeader * hdr)
{
    HttpHdrCc *cc;
    String s; 
    if (!CBIT_TEST(hdr->mask, HDR_CACHE_CONTROL))
        return NULL;
    s = httpHeaderGetList(hdr, HDR_CACHE_CONTROL);
    cc = httpHdrCcParseCreate(&s);
    HttpHeaderStats[hdr->owner].ccParsedCount++;
    if (cc)
        httpHdrCcUpdateStats(cc, &HttpHeaderStats[hdr->owner].ccTypeDistr);
    httpHeaderNoteParsedEntry(HDR_CACHE_CONTROL, s, !cc);
    stringClean(&s);
    return cc;
}
