
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
#include "../include/util.h"
#include "../include/Vector.h"
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

/* XXX just for the mempool initialisation hackery */
#include "HttpHdrRange.h"
#include "HttpHdrContRange.h"

HttpHeaderFieldInfo *Headers = NULL;
MemPool * pool_http_header_entry = NULL;

/* XXX these shouldn't be here? */
MemPool * pool_http_hdr_range_spec = NULL;
MemPool * pool_http_hdr_range = NULL;
MemPool * pool_http_hdr_cont_range = NULL;

/* XXX these masks probably shouldn't be here */
HttpHeaderMask ListHeadersMask;  /* set run-time using  ListHeadersArr */
HttpHeaderMask ReplyHeadersMask;         /* set run-time using ReplyHeaders */
HttpHeaderMask RequestHeadersMask;       /* set run-time using RequestHeaders */

void
httpHeaderInitLibrary(void)
{
    int i;

    /* check that we have enough space for masks */
    assert(8 * sizeof(HttpHeaderMask) >= HDR_ENUM_END);

    /* all headers must be described */
    assert(HeadersAttrsCount == HDR_ENUM_END);
    if (!Headers)
        Headers = httpHeaderBuildFieldsInfo(HeadersAttrs, HDR_ENUM_END);
    if (! pool_http_header_entry)
        pool_http_header_entry = memPoolCreate("HttpHeaderEntry", sizeof(HttpHeaderEntry));

    /* XXX these shouldn't be here; the header code shouldn't rely on the range code just for initialisation! */
    /* XXX so shuffle these out into their relevant modules later on and remove the above #include! */
    if (! pool_http_hdr_range_spec)
        pool_http_hdr_range_spec = memPoolCreate("HttpHdrRangeSpec", sizeof(HttpHdrRangeSpec));
    if (! pool_http_hdr_range)
        pool_http_hdr_range = memPoolCreate("HttpHdrRange", sizeof(HttpHdrRange));
    if (! pool_http_hdr_cont_range)
        pool_http_hdr_cont_range = memPoolCreate("HttpHdrContRange", sizeof(HttpHdrContRange));

    /* create masks */
    /* XXX should only do this once, no? */
    httpHeaderMaskInit(&ListHeadersMask, 0);
    httpHeaderCalcMask(&ListHeadersMask, ListHeadersArr, ListHeadersArrCount);
    httpHeaderMaskInit(&ReplyHeadersMask, 0);
    httpHeaderCalcMask(&ReplyHeadersMask, ReplyHeadersArr, ReplyHeadersArrCount);
    httpHeaderCalcMask(&ReplyHeadersMask, GeneralHeadersArr, GeneralHeadersArrCount);
    httpHeaderCalcMask(&ReplyHeadersMask, EntityHeadersArr, EntityHeadersArrCount);
    httpHeaderMaskInit(&RequestHeadersMask, 0);
    httpHeaderCalcMask(&RequestHeadersMask, RequestHeadersArr, RequestHeadersArrCount);
    httpHeaderCalcMask(&RequestHeadersMask, GeneralHeadersArr, GeneralHeadersArrCount);
    httpHeaderCalcMask(&RequestHeadersMask, EntityHeadersArr, EntityHeadersArrCount);

    /* init header stats */
    assert(HttpHeaderStatCount == hoReply + 1);
    for (i = 0; i < HttpHeaderStatCount; i++)
        httpHeaderStatInit(HttpHeaderStats + i, HttpHeaderStats[i].label);
    HttpHeaderStats[hoRequest].owner_mask = &RequestHeadersMask;
    HttpHeaderStats[hoReply].owner_mask = &ReplyHeadersMask;
#if USE_HTCP
    HttpHeaderStats[hoHtcpReply].owner_mask = &ReplyHeadersMask;
#endif
}

/*!
 * @function
 *	httpHeaderInit
 * @abstract
 *	Initialize a HttpHeader set.
 *
 * @param	hdr	pointer to HttpHeader to initalize.
 * @param	owner	what kind of http state owns this - request or reply.
 *
 * @discussion
 *	Use this to initialize a HttpHeader that is embedded into some other
 *	"state" structure.
 *
 *	It bzero's the region, sets up the owner type and initializes the entry
 *	array.
 */
void
httpHeaderInit(HttpHeader * hdr, http_hdr_owner_type owner)
{
    assert(hdr);
    assert(owner > hoNone && owner <= hoReply);
    debugs(55, 7, "init-ing hdr: %p owner: %d", hdr, owner);
    memset(hdr, 0, sizeof(*hdr));
    hdr->owner = owner;
    arrayInit(&hdr->entries);
}

/*!
 * @function
 *	httpHeaderClean
 * @abstract
 *	Free the data associated with this HttpHeader; keep statistics.
 *
 * @param	hdr	HttpHeader to clean.
 *
 * @discussion
 *	Plenty of existing code seems to initialize and clean HttpHeader
 *	structs before anything is parsed; the statistics for "0" headers
 *	therefore would be massively skewed and is thus not kept.
 *
 *	Any HttpHeaderEntry items in the hdr->entries Array are destroyed,
 *	regardless whether anything is referencing them (they shouldn't be!)
 *
 *	This highlights the desire not to use HttpHeaderEntry pointers
 *	for anything other than for quick, ephemeral working.
 */
void
httpHeaderClean(HttpHeader * hdr)
{
    HttpHeaderPos pos = HttpHeaderInitPos;
    HttpHeaderEntry *e;

    assert(hdr);
    assert(hdr->owner > hoNone && hdr->owner <= hoReply);
    debugs(55, 7, "cleaning hdr: %p owner: %d", hdr, hdr->owner);

    /*
     * An unfortunate bug.  The hdr->entries array is initialized
     * such that count is set to zero.  httpHeaderClean() seems to
     * be called both when 'hdr' is created, and destroyed.  Thus,
     * we accumulate a large number of zero counts for 'hdr' before
     * it is ever used.  Can't think of a good way to fix it, except
     * adding a state variable that indicates whether or not 'hdr'
     * has been used.  As a hack, just never count zero-sized header
     * arrays.
     */
    if (0 != hdr->entries.count)
        statHistCount(&HttpHeaderStats[hdr->owner].hdrUCountDistr, hdr->entries.count);
    HttpHeaderStats[hdr->owner].destroyedCount++;
    HttpHeaderStats[hdr->owner].busyDestroyedCount += hdr->entries.count > 0;
    while ((e = httpHeaderGetEntry(hdr, &pos))) {
        /* tmp hack to try to avoid coredumps */
        if (e->id >= HDR_ENUM_END) {
            debugs(55, 0, "httpHeaderClean BUG: entry[%d] is invalid (%d). Ignored.",
                (int) pos, e->id);
        } else {
            statHistCount(&HttpHeaderStats[hdr->owner].fieldTypeDistr, e->id);
            /* yes, this destroy() leaves us in an inconsistent state */
            httpHeaderEntryDestroy(e);
	    memPoolFree(pool_http_header_entry, e);
        }
    }
    arrayClean(&hdr->entries);
}

/* just handy in parsing: resets and returns false */
/*!
 * @function
 *	httpHeaderReset
 * @abstract
 *	Reset the current header back to the initial state after the
 *	first httpHeaderInit() call, deallocating the header entries
 *	and (in the future) resetting the parser state.
 *
 *	This function returns 0.
 *
 * @discussion
 *	The http header parser doesn't currently handle incremental
 *	parsing - this routine gets called quite a bit when parsing
 *	fails for any reason.
 *
 *	The function returns 0 so it can be called in a return; eg
 *	'return httpHeaderReset(hdr)' to reset the headers and
 *	return false.
 *
 * @param	hdr	HttpHeader to reset state
 * @return		0 (false)
 */
int 
httpHeaderReset(HttpHeader * hdr)
{
    http_hdr_owner_type ho = hdr->owner;
    assert(hdr);
    ho = hdr->owner;
    httpHeaderClean(hdr);
    httpHeaderInit(hdr, ho);
    return 0;
}   

static void
httpHeaderAddInfo(HttpHeader *hdr, HttpHeaderEntry *e)
{
    assert(hdr && e);
    assert_eid(e->id);

    if (CBIT_TEST(hdr->mask, e->id))
        Headers[e->id].stat.repCount++;
    else
        CBIT_SET(hdr->mask, e->id);
    /* increment header length, allow for ": " and crlf */
    hdr->len += strLen(e->name) + 2 + strLen(e->value) + 2;
}

/* appends an entry;
 * does not call httpHeaderEntryClone() so one should not reuse "*e"
 */
void
httpHeaderAddEntry(HttpHeader * hdr, HttpHeaderEntry * e)
{
    debugs(55, 7, "%p adding entry: %d at %d", hdr, e->id, hdr->entries.count);
    httpHeaderAddInfo(hdr, e);
    arrayAppend(&hdr->entries, e);
}

/*!
 * @function
 *	httpHeaderAddEntryStr
 * @abstract
 *	Append a header entry to the give hdr.
 *
 * @param	hdr		HttpHeader to append the entry to.
 * @param	id		http_hdr_type; HDR_OTHER == uses NUL-terminated attrib;
 *				else attrib is ignored.
 * @param	value		NUL-terminated header value.
 *
 * @discussion
 *	The attrib/value strings will be copied even if the values passed in are static.
 *
 *	strlen() will be run on the strings as appropriate. Call httpHeaderEntryAddStr2() if
 *	the length of the string is known up front.
 *
 *	For now this routine simply calls httpHeaderEntryCreate() to create the entry and
 *	then httpHeaderAddEntry() to add it; the plan is to eliminate the httpHeaderEntryCreate()
 *	allocator overhead.
 */
void
httpHeaderAddEntryStr(HttpHeader *hdr, http_hdr_type id, const char *attrib, const char *value)
{
	httpHeaderAddEntryStr2(hdr, id, attrib, -1, value, -1);
}

/*
 * -1 means "don't know length, call strlen()
 */
int
httpHeaderAddEntryStr2(HttpHeader *hdr, http_hdr_type id, const char *a, int al, const char *v, int vl)
{
	HttpHeaderEntry *e = memPoolAlloc(pool_http_header_entry);
	httpHeaderEntryCreate(e, id, a, al, v, vl);
	httpHeaderAddEntry(hdr, e);
	return(hdr->entries.count - 1);
}

void
httpHeaderAddEntryString(HttpHeader *hdr, http_hdr_type id, const String *a, const String *v)
{
	HttpHeaderEntry *e = memPoolAlloc(pool_http_header_entry);
	httpHeaderEntryCreateStr(e, id, a, v);
	httpHeaderAddEntry(hdr, e);
}

/*!
 * @function
 *	httpHeaderInsertEntryStr
 * @abstract
 *	Insert a header entry into the give hdr at position pos
 *
 * @param	hdr		HttpHeader to append the entry to.
 * @param	pos		position in the array to insert.
 * @param	id		http_hdr_type; HDR_OTHER == uses NUL-terminated attrib;
 *				else attrib is ignored.
 * @param	value		NUL-terminated header value.
 *
 * @discussion
 *	The attrib/value strings will be copied even if the values passed in are static.
 *
 *	strlen() will be run on the strings as appropriate. Call httpHeaderEntryAddStr2() if
 *	the length of the string is known up front.
 *
 *	For now this routine simply calls httpHeaderEntryCreate() to create the entry and
 *	then httpHeaderInsertEntry() to add it; the plan is to eliminate the httpHeaderEntryCreate()
 *	allocator overhead.
 */
void
httpHeaderInsertEntryStr(HttpHeader *hdr, int pos, http_hdr_type id, const char *attrib, const char *value)
{
	HttpHeaderEntry *e = memPoolAlloc(pool_http_header_entry);
	httpHeaderEntryCreate(e, id, attrib, -1, value, -1);
	httpHeaderInsertEntry(hdr, e, pos);
}

/* inserts an entry at the given position;
 * does not call httpHeaderEntryClone() so one should not reuse "*e"
 */
void
httpHeaderInsertEntry(HttpHeader * hdr, HttpHeaderEntry * e, int pos)
{
    debugs(55, 7, "%p adding entry: %d at %d", hdr, e->id, hdr->entries.count);
    httpHeaderAddInfo(hdr, e);
    arrayInsert(&hdr->entries, e, pos);
}

/* returns next valid entry */
HttpHeaderEntry *
httpHeaderGetEntry(const HttpHeader * hdr, HttpHeaderPos * pos)
{
    assert(hdr && pos);
    assert(*pos >= HttpHeaderInitPos && *pos < hdr->entries.count);
    for ((*pos)++; *pos < hdr->entries.count; (*pos)++) {
        if (hdr->entries.items[*pos])
            return hdr->entries.items[*pos];
    }
    return NULL;
}

void
httpHeaderAddClone(HttpHeader * hdr, const HttpHeaderEntry * e)
{
    HttpHeaderEntry *ne = memPoolAlloc(pool_http_header_entry);
    httpHeaderEntryClone(ne, e);
    httpHeaderAddEntry(hdr, ne);
}

/*!
 * @function
 *	httpHeaderAppend
 * @abstract
 *	Append the headers from "src" to the end of "dest"
 *
 * @param	dest		HttpHeader to copy headers to.
 * @param	src		HttpHeader to copy headers from.
 *
 * @discussion
 *
 *	Headers are cloned correctly - so both modifying and freeing
 *	the entries in "src" will not affect the entries in "dst".
 */
void
httpHeaderAppend(HttpHeader * dest, const HttpHeader * src)
{
    const HttpHeaderEntry *e;
    HttpHeaderPos pos = HttpHeaderInitPos;
    assert(src && dest);
    assert(src != dest);
    debugs(55, 7, "appending hdr: %p += %p", dest, src);

    while ((e = httpHeaderGetEntry(src, &pos))) {
        httpHeaderAddClone(dest, e);
    }
}

/*
 * deletes all fields with a given name if any, returns #fields deleted;
 */
int
httpHeaderDelByName(HttpHeader * hdr, const char *name)
{
    int count = 0;
    HttpHeaderPos pos = HttpHeaderInitPos;
    HttpHeaderEntry *e;
    httpHeaderMaskInit(&hdr->mask, 0);  /* temporal inconsistency */
    debugs(55, 7, "deleting '%s' fields in hdr %p", name, hdr);
    while ((e = httpHeaderGetEntry(hdr, &pos))) {
        if (!strCaseCmp(e->name, name)) {
            httpHeaderDelAt(hdr, pos);
            count++;
        } else
            CBIT_SET(hdr->mask, e->id);
    }
    return count;
}

/* deletes all entries with a given id, returns the #entries deleted */
int
httpHeaderDelById(HttpHeader * hdr, http_hdr_type id)
{
    int count = 0;
    HttpHeaderPos pos = HttpHeaderInitPos;
    HttpHeaderEntry *e;
    debugs(55, 8, "%p del-by-id %d", hdr, id);
    assert(hdr);
    assert_eid(id);
    assert(id != HDR_OTHER);    /* does not make sense */
    if (!CBIT_TEST(hdr->mask, id))
        return 0;
    while ((e = httpHeaderGetEntry(hdr, &pos))) {
        if (e->id == id) {
            httpHeaderDelAt(hdr, pos);
            count++;
        }
    }
    CBIT_CLR(hdr->mask, id);
    assert(count);
    return count;
}

/*
 * deletes an entry at pos and leaves a gap; leaving a gap makes it
 * possible to iterate(search) and delete fields at the same time
 * WARNING: Doesn't update the header mask. Call httpHeaderRefreshMask
 * when done with the delete operations.
 */
void
httpHeaderDelAt(HttpHeader * hdr, HttpHeaderPos pos)
{
    HttpHeaderEntry *e;
    assert(pos >= HttpHeaderInitPos && pos < hdr->entries.count);
    e = hdr->entries.items[pos];
    hdr->entries.items[pos] = NULL;
    /* decrement header length, allow for ": " and crlf */
    hdr->len -= strLen(e->name) + 2 + strLen(e->value) + 2;
    assert(hdr->len >= 0);
    httpHeaderEntryDestroy(e);
    memPoolFree(pool_http_header_entry, e);
}

int
httpHeaderIdByName(const char *name, int name_len, const HttpHeaderFieldInfo * info, int end)
{
    int i;
    for (i = 0; i < end; ++i) {
        if (name_len >= 0 && name_len != strLen(info[i].name))
            continue;
        if (!strncasecmp(name, strBuf(info[i].name),
                name_len < 0 ? strLen(info[i].name) + 1 : name_len))
            return i;
    }
    return -1;
}

HttpHeaderEntry *
httpHeaderFindEntry(const HttpHeader * hdr, http_hdr_type id)
{
    HttpHeaderPos pos = HttpHeaderInitPos;
    HttpHeaderEntry *e;
    assert(hdr);
    assert_eid(id);
    assert(!CBIT_TEST(ListHeadersMask, id));

    /* check mask first */
    if (!CBIT_TEST(hdr->mask, id))
        return NULL;
    /* looks like we must have it, do linear search */
    while ((e = httpHeaderGetEntry(hdr, &pos))) {
        if (e->id == id)
            return e;
    }
    /* hm.. we thought it was there, but it was not found */
    assert(0);
    return NULL;                /* not reached */
}

/*
 * same as httpHeaderFindEntry
 */
HttpHeaderEntry *
httpHeaderFindLastEntry(const HttpHeader * hdr, http_hdr_type id)
{
    HttpHeaderPos pos = HttpHeaderInitPos;
    HttpHeaderEntry *e;
    HttpHeaderEntry *result = NULL;
    assert(hdr);
    assert_eid(id);
    assert(!CBIT_TEST(ListHeadersMask, id));

    /* check mask first */
    if (!CBIT_TEST(hdr->mask, id))
        return NULL;
    /* looks like we must have it, do linear search */
    while ((e = httpHeaderGetEntry(hdr, &pos))) {
        if (e->id == id)
            result = e;
    }
    assert(result);             /* must be there! */
    return result;
}

/*
 * Returns the value of the specified header. 
 */     
String      
httpHeaderGetByName(const HttpHeader * hdr, const char *name)
{       
    http_hdr_type id;
    HttpHeaderPos pos = HttpHeaderInitPos;
    HttpHeaderEntry *e;
    String result = StringNull;

    assert(hdr);
    assert(name);

    /* First try the quick path */
    id = httpHeaderIdByNameDef(name, strlen(name));
    if (id != -1)
        return httpHeaderGetStrOrList(hdr, id);
    
    /* Sorry, an unknown header name. Do linear search */
    while ((e = httpHeaderGetEntry(hdr, &pos))) {
        if (e->id == HDR_OTHER && strCaseCmp(e->name, name) == 0) {
            strListAddStr(&result, strBuf2(e->value), strLen2(e->value), ',');
        }
    }
    return result;
}   

int
httpHeaderIdByNameDef(const char *name, int name_len)
{
    if (!Headers)
        Headers = httpHeaderBuildFieldsInfo(HeadersAttrs, HDR_ENUM_END);
    return httpHeaderIdByName(name, name_len, Headers, HDR_ENUM_END);
}

const char *
httpHeaderNameById(int id)
{
    if (!Headers)
        Headers = httpHeaderBuildFieldsInfo(HeadersAttrs, HDR_ENUM_END);
    assert(id >= 0 && id < HDR_ENUM_END);
    return strBuf(Headers[id].name);
}

/* test if a field is present */
int 
httpHeaderHas(const HttpHeader * hdr, http_hdr_type id)
{
    assert(hdr);
    assert_eid(id);
    assert(id != HDR_OTHER);
    debugs(55, 7, "%p lookup for %d", hdr, id);
    return CBIT_TEST(hdr->mask, id); 
}

/*  
 * Refreshes the header mask. Useful after httpHeaderDelAt constructs
 */ 
void
httpHeaderRefreshMask(HttpHeader * hdr)
{
    HttpHeaderPos pos = HttpHeaderInitPos;
    HttpHeaderEntry *e;
    httpHeaderMaskInit(&hdr->mask, 0);
    debugs(55, 7, "refreshing the mask in hdr %p", hdr);
    while ((e = httpHeaderGetEntry(hdr, &pos))) {
        CBIT_SET(hdr->mask, e->id);
    }
}

void
httpHeaderRepack(HttpHeader * hdr)
{
    HttpHeaderPos dp = HttpHeaderInitPos;
    HttpHeaderPos pos = HttpHeaderInitPos;
    
    /* XXX breaks layering for now! ie, getting grubby fingers in without httpHeaderEntryGet() */
    dp = 0;
    pos = 0;
    while (dp < hdr->entries.count) {
        for (; dp < hdr->entries.count && hdr->entries.items[dp] == NULL; dp++);
        if (dp >= hdr->entries.count)
            break;
        hdr->entries.items[pos] = hdr->entries.items[dp];
        if (dp != pos)
            hdr->entries.items[dp] = NULL;
        pos++;
        dp++;
    }   
    arrayShrink(&hdr->entries, pos);
}   

/* use fresh entries to replace old ones */
void
httpHeaderUpdate(HttpHeader * old, const HttpHeader * fresh, const HttpHeaderMask * denied_mask)
{
    const HttpHeaderEntry *e;
    HttpHeaderPos pos = HttpHeaderInitPos;

    assert(old && fresh);
    assert(old != fresh);
    debugs(55, 7, "updating hdr: %p <- %p", old, fresh);

    while ((e = httpHeaderGetEntry(fresh, &pos))) {
        /* deny bad guys (ok to check for HDR_OTHER) here */
        if (denied_mask && CBIT_TEST(*denied_mask, e->id))
            continue;
        if (e->id != HDR_OTHER)
            httpHeaderDelById(old, e->id);
        else
            httpHeaderDelByName(old, strBuf(e->name));
    }
    pos = HttpHeaderInitPos;
    while ((e = httpHeaderGetEntry(fresh, &pos))) {
        /* deny bad guys (ok to check for HDR_OTHER) here */
        if (denied_mask && CBIT_TEST(*denied_mask, e->id))
            continue;
        httpHeaderAddClone(old, e);
    }
    
    /* And now, repack the array to "fill in the holes" */
    httpHeaderRepack(old);
}

