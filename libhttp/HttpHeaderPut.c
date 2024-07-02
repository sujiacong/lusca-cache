
/*
 * $Id: HttpHeader.c 12651 2008-04-25 16:47:11Z adrian.chadd $
 *
 * DEBUG: section 55    HTTP Header
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
#include "HttpHeaderPut.h"

static void httpHeaderPutStrvf(HttpHeader * hdr, http_hdr_type id, const char *fmt, va_list vargs);

void
httpHeaderPutStr(HttpHeader * hdr, http_hdr_type id, const char *str)
{   
    assert_eid(id);
    assert(Headers[id].type == ftStr);  /* must be of an appropriate type */
    assert(str);
    httpHeaderAddEntryStr(hdr, id, NULL, str);
}

void
httpHeaderPutStr2(HttpHeader * hdr, http_hdr_type id, const char *str, int len)
{   
    assert_eid(id);
    assert(Headers[id].type == ftStr);  /* must be of an appropriate type */
    assert(str);
    httpHeaderAddEntryStr2(hdr, id, NULL, -1, str, len);
}

void
httpHeaderPutString(HttpHeader *hdr, http_hdr_type id, String *s)
{
    assert_eid(id);
    assert(Headers[id].type == ftStr);  /* must be of an appropriate type */
    assert(s);
    httpHeaderAddEntryString(hdr, id, NULL, s);
}

/* same as httpHeaderPutStr, but formats the string using snprintf first */
void
#if STDC_HEADERS
httpHeaderPutStrf(HttpHeader * hdr, http_hdr_type id, const char *fmt,...)
#else
httpHeaderPutStrf(va_alist)
     va_dcl
#endif
{
#if STDC_HEADERS
    va_list args;
    va_start(args, fmt);
#else
    va_list args;
    HttpHeader *hdr = NULL;
    http_hdr_type id = HDR_ENUM_END;
    const char *fmt = NULL;
    va_start(args);
    hdr = va_arg(args, HttpHeader *);
    id = va_arg(args, http_hdr_type);
    fmt = va_arg(args, char *);
#endif
    httpHeaderPutStrvf(hdr, id, fmt, args);
    va_end(args);
}

/* used by httpHeaderPutStrf */
static void
httpHeaderPutStrvf(HttpHeader * hdr, http_hdr_type id, const char *fmt, va_list vargs)
{
    MemBuf mb;
    memBufDefInit(&mb);
    memBufVPrintf(&mb, fmt, vargs);
    httpHeaderPutStr(hdr, id, mb.buf);
    memBufClean(&mb);
}

void
httpHeaderPutInt(HttpHeader * hdr, http_hdr_type id, int number)
{   
    assert_eid(id);
    assert(Headers[id].type == ftInt);  /* must be of an appropriate type */
    assert(number >= 0);
    httpHeaderAddEntryStr(hdr, id, NULL, xitoa(number));
}

void
httpHeaderPutSize(HttpHeader * hdr, http_hdr_type id, squid_off_t number)
{   
    char size[64];
    assert_eid(id);
    assert(Headers[id].type == ftSize);         /* must be of an appropriate type */
    assert(number >= 0);
    snprintf(size, sizeof(size), "%" PRINTF_OFF_T, number);
    httpHeaderAddEntryStr(hdr, id, NULL, size);
}

void
httpHeaderPutTime(HttpHeader * hdr, http_hdr_type id, time_t htime)
{   
    assert_eid(id);
    assert(Headers[id].type == ftDate_1123);    /* must be of an appropriate type */
    assert(htime >= 0);
    httpHeaderAddEntryStr(hdr, id, NULL, mkrfc1123(htime));
}

void
httpHeaderInsertTime(HttpHeader * hdr, int pos, http_hdr_type id, time_t htime)
{   
    assert_eid(id);
    assert(Headers[id].type == ftDate_1123);    /* must be of an appropriate type */
    assert(htime >= 0);
    httpHeaderInsertEntryStr(hdr, pos, id, NULL, mkrfc1123(htime));
}


void
httpHeaderPutAuth(HttpHeader * hdr, const char *auth_scheme, const char *realm)
{   
    assert(hdr && auth_scheme && realm);
    httpHeaderPutStrf(hdr, HDR_WWW_AUTHENTICATE, "%s realm=\"%s\"", auth_scheme, realm);
}

/* add extension header (these fields are not parsed/analyzed/joined, etc.) */
void
httpHeaderPutExt(HttpHeader * hdr, const char *name, const char *value, int value_len)
{
    assert(name && value);
    debug(55, 8) ("%p adds ext entry '%s: %.*s'\n", hdr, name, value_len, value);
    httpHeaderAddEntryStr2(hdr, HDR_OTHER, name, -1, value, value_len);
}

