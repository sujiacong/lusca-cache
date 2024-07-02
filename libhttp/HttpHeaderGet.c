
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
#include <ctype.h>

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

int
httpHeaderGetInt(const HttpHeader * hdr, http_hdr_type id)
{   
    HttpHeaderEntry *e;
    int value = -1;
    int ok;
    assert_eid(id);
    assert(Headers[id].type == ftInt);  /* must be of an appropriate type */
    if ((e = httpHeaderFindEntry(hdr, id))) {
        ok = httpHeaderParseInt(strBuf(e->value), &value);
        httpHeaderNoteParsedEntry(e->id, e->value, !ok);
    }
    return value;
}

squid_off_t
httpHeaderGetSize(const HttpHeader * hdr, http_hdr_type id)
{   
    HttpHeaderEntry *e; 
    squid_off_t value = -1;
    int ok;
    assert_eid(id);
    assert(Headers[id].type == ftSize);         /* must be of an appropriate type */
    if ((e = httpHeaderFindEntry(hdr, id))) {
        ok = httpHeaderParseSize(strBuf(e->value), &value);
        httpHeaderNoteParsedEntry(e->id, e->value, !ok);
    }
    return value;
}

time_t
httpHeaderGetTime(const HttpHeader * hdr, http_hdr_type id)
{
    HttpHeaderEntry *e;
    time_t value = -1;
    assert_eid(id);
    assert(Headers[id].type == ftDate_1123);    /* must be of an appropriate type */
    if ((e = httpHeaderFindEntry(hdr, id))) {
        value = parse_rfc1123(strBuf(e->value), strLen(e->value));
        httpHeaderNoteParsedEntry(e->id, e->value, value < 0);
    }
    return value;
}

/* sync with httpHeaderGetLastStr */
const char *
httpHeaderGetStr(const HttpHeader * hdr, http_hdr_type id)
{   
    HttpHeaderEntry *e;
    assert_eid(id);
    assert(Headers[id].type == ftStr);  /* must be of an appropriate type */
    if ((e = httpHeaderFindEntry(hdr, id))) {
        httpHeaderNoteParsedEntry(e->id, e->value, 0);  /* no errors are possible */
        return strBuf(e->value);
    }
    return NULL;
}

/* unusual */
const char *
httpHeaderGetLastStr(const HttpHeader * hdr, http_hdr_type id)
{   
    HttpHeaderEntry *e;
    assert_eid(id);
    assert(Headers[id].type == ftStr);  /* must be of an appropriate type */
    if ((e = httpHeaderFindLastEntry(hdr, id))) {
        httpHeaderNoteParsedEntry(e->id, e->value, 0);  /* no errors are possible */
        return strBuf(e->value);
    }
    return NULL;
}


TimeOrTag
httpHeaderGetTimeOrTag(const HttpHeader * hdr, http_hdr_type id)
{
    TimeOrTag tot;
    HttpHeaderEntry *e;
    assert(Headers[id].type == ftDate_1123_or_ETag);    /* must be of an appropriate type */
    memset(&tot, 0, sizeof(tot));
    if ((e = httpHeaderFindEntry(hdr, id))) {
        const char *str = strBuf(e->value); 
        /* try as an ETag */
        if (*str == '"' || (str[0] == 'W' && str[1] == '/')) {
            tot.tag = str; 
            tot.time = -1;
            tot.valid = 1;
        } else {
            /* or maybe it is time? */
            tot.time = parse_rfc1123(str, strLen(e->value));
            if (tot.time >= 0)
                tot.valid = 1;
            tot.tag = NULL; 
        }   
    } else {
        tot.time = -1;
    }   
    return tot;
}

const char *
httpHeaderGetAuth(const HttpHeader * hdr, http_hdr_type id, const char *auth_scheme)
{
    const char *field;
    int l;
    assert(hdr && auth_scheme);
    field = httpHeaderGetStr(hdr, id); 
    if (!field)                 /* no authorization field */
        return NULL;
    l = strlen(auth_scheme);
    if (!l || strncasecmp(field, auth_scheme, l))       /* wrong scheme */
        return NULL;
    field += l;
    if (!xisspace(*field))      /* wrong scheme */
        return NULL;
    /* skip white space */
    field += xcountws(field);
    if (!*field)                /* no authorization cookie */
        return NULL;
    return base64_decode(field);
}

