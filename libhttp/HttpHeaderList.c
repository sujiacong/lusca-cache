
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


/* return a list of entries with the same id separated by ',' and ws */
String
httpHeaderGetList(const HttpHeader * hdr, http_hdr_type id)
{   
    String s = StringNull;
    HttpHeaderEntry *e;
    HttpHeaderPos pos = HttpHeaderInitPos;
    debugs(55, 6, "%p: joining for id %d", hdr, id);
    /* only fields from ListHeaders array can be "listed" */
    assert(CBIT_TEST(ListHeadersMask, id));
    if (!CBIT_TEST(hdr->mask, id))
        return s;
    while ((e = httpHeaderGetEntry(hdr, &pos))) {
        if (e->id == id)
            strListAddStr(&s, strBuf2(e->value), strLen2(e->value), ',');
    }
    /*
     * note: we might get an empty (len==0) string if there was an "empty"
     * header; we must not get a NULL string though.
     */
    assert(strBuf(s));
    /* temporary warning: remove it! @?@ @?@ @?@ */
    if (!strLen(s))
        debugs(55, 3, "empty list header: %.*s (%d)", strLen2(Headers[id].name), strBuf2(Headers[id].name), id);
    debugs(55, 6, "%p: joined for id %d: %.*s", hdr, id, strLen2(s), strBuf2(s));
    return s;
}

/* return a string or list of entries with the same id separated by ',' and ws */
String
httpHeaderGetStrOrList(const HttpHeader * hdr, http_hdr_type id)
{
    HttpHeaderEntry *e;

    if (CBIT_TEST(ListHeadersMask, id))
        return httpHeaderGetList(hdr, id);
    if ((e = httpHeaderFindEntry(hdr, id))) {
        return(stringDup(&e->value));
    }
    return StringNull;
}

/* 
 * returns a pointer to a specified entry if any 
 * note that we return one entry so it does not make much sense to ask for
 * "list" headers
 */
String
httpHeaderGetByNameListMember(const HttpHeader * hdr, const char *name, const char *member, const char separator)
{   
    String result = StringNull;
    String header;
    const char *pos = NULL;
    const char *item;
    int ilen;
    int mlen = strlen(member);
    
    assert(hdr);
    assert(name);
    
    header = httpHeaderGetByName(hdr, name);
    
    while (strListGetItem(&header, separator, &item, &ilen, &pos)) {
        if (strncmp(item, member, mlen) == 0 && item[mlen] == '=') {
            stringAppend(&result, item + mlen + 1, ilen - mlen - 1);
            break;
        }
    }
    stringClean(&header);
    return result;
}

/*
 * returns a the value of the specified list member, if any.
 */
String
httpHeaderGetListMember(const HttpHeader * hdr, http_hdr_type id, const char *member, const char separator)
{   
    String result = StringNull;
    String header;
    const char *pos = NULL;
    const char *item;
    int ilen;
    int mlen = strlen(member);
    
    assert(hdr);
    assert_eid(id);
    
    header = httpHeaderGetStrOrList(hdr, id);
    
    while (strListGetItem(&header, separator, &item, &ilen, &pos)) {
        if (strncmp(item, member, mlen) == 0 && item[mlen] == '=') {
            stringAppend(&result, item + mlen + 1, ilen - mlen - 1);
            break;
        }
    }
    stringClean(&header);
    return result;
}

