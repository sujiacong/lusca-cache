
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

#include "../libcb/cbdata.h"

#include "HttpVersion.h"
#include "HttpStatusLine.h"
#include "HttpHeaderType.h"
#include "HttpHeaderFieldStat.h"
#include "HttpHeaderFieldInfo.h"
#include "HttpHeaderEntry.h"
#include "HttpHeader.h"
#include "HttpHeaderTools.h"

/*
 * HttpHeaderEntry
 */

/* XXX old functions */

/*
 * A length of -1 means "unknown; call strlen()
 */
void
httpHeaderEntryCreate(HttpHeaderEntry *e, http_hdr_type id, const char *name, int al, const char *value, int vl)
{
    assert_eid(id);
    assert(! e->active);
    e->id = id;
    if (id != HDR_OTHER)
        e->name = Headers[id].name;
    else if (al == -1)
        stringInit(&e->name, name);
    else
        stringLimitInit(&e->name, name, al);
    if (vl == -1)
        stringInit(&e->value, value);
    else
        stringLimitInit(&e->value, value, vl);
    Headers[id].stat.aliveCount++;
    debugs(55, 9, "created entry %p: '%.*s: %.*s'", e, strLen2(e->name), strBuf2(e->name), strLen2(e->value), strBuf2(e->value));
    e->active = 1;
}

void
httpHeaderEntryCreateStr(HttpHeaderEntry *e, http_hdr_type id, const String *name, const String *value)
{
    assert_eid(id);
    assert(! e->active);
    e->id = id;
    if (id != HDR_OTHER)
        e->name = Headers[id].name;
    else
	e->name = stringDup(name);
    e->value = stringDup(value);
    Headers[id].stat.aliveCount++;
    debugs(55, 9, "created entry %p: '%.*s: %.*s'", e, strLen2(e->name), strBuf2(e->name), strLen2(e->value), strBuf2(e->value));
    e->active = 1;
}

void
httpHeaderEntryDestroy(HttpHeaderEntry * e)
{
    assert(e);
    assert_eid(e->id);
    debugs(55, 9, "destroying entry %p: '%.*s: %.*s'", e, strLen2(e->name), strBuf2(e->name), strLen2(e->value), strBuf2(e->value));
    /* clean name if needed */
    if (e->id == HDR_OTHER)
        stringClean(&e->name);
    stringClean(&e->value);
    assert(Headers[e->id].stat.aliveCount);
    Headers[e->id].stat.aliveCount--;
    e->id = -1;
    e->active = 0;
}

void
httpHeaderEntryClone(HttpHeaderEntry *new_e, const HttpHeaderEntry * e)
{
    httpHeaderEntryCreateStr(new_e, e->id, &e->name, &e->value);
}

