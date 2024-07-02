
/*
 * $Id: memstr.c,v 1.1.2.1 2007/12/06 08:10:26 adri Exp $
 *
 * DEBUG: section 13    High Level Memory Pool Management
 * AUTHOR: Harvest Derived
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
#include <sys/types.h>
#include <string.h>

#include "../include/util.h"
#include "../include/Stack.h"
#include "../libcore/valgrind.h"
#include "../libcore/gb.h"
#include "../libcore/varargs.h" /* required for tools.h */
#include "../libcore/tools.h"
#include "../libcore/debug.h"

#include "MemPool.h"
#include "MemStr.h"

/* module globals */

/* string pools */
#define MEM_STR_POOL_COUNT 3
static const struct {
    const char *name;
    size_t obj_size;
} StrPoolsAttrs[MEM_STR_POOL_COUNT] = {

    {
	"Short Strings", 36,
    },				/* to fit rfc1123 and similar */
    {
	"Medium Strings", 128,
    },				/* to fit most urls */
    {
	"Long Strings", 512
    }				/* other */
};

StrPoolsStruct StrPools[MEM_STR_POOL_COUNT];
MemMeter StrCountMeter;
MemMeter StrVolumeMeter;

/* local routines */

/* allocate a variable size buffer using best-fit pool */
void *
memAllocString(size_t net_size, size_t * gross_size)
{
    int i;
    MemPool *pool = NULL;
    assert(gross_size);
    for (i = 0; i < MEM_STR_POOL_COUNT; i++) {
	if (net_size <= StrPoolsAttrs[i].obj_size) {
	    pool = StrPools[i].pool;
	    break;
	}
    }
    *gross_size = pool ? pool->obj_size : net_size;
    assert(*gross_size >= net_size);
    memMeterInc(StrCountMeter);
    memMeterAdd(StrVolumeMeter, *gross_size);
    return pool ? memPoolAlloc(pool) : xmalloc(net_size);
}

/* free buffer allocated with memAllocString() */
void
memFreeString(size_t size, void *buf)
{
    int i;
    MemPool *pool = NULL;
    assert(size && buf);
    for (i = 0; i < MEM_STR_POOL_COUNT; i++) {
	if (size <= StrPoolsAttrs[i].obj_size) {
	    assert(size == StrPoolsAttrs[i].obj_size);
	    pool = StrPools[i].pool;
	    break;
	}
    }
    memMeterDec(StrCountMeter);
    memMeterDel(StrVolumeMeter, size);
    pool ? memPoolFree(pool, buf) : xfree(buf);
}

void
memStringInit(void)
{
    int i;
    /* init string pools */
    for (i = 0; i < MEM_STR_POOL_COUNT; i++) {
	StrPools[i].pool = memPoolCreate(StrPoolsAttrs[i].name, StrPoolsAttrs[i].obj_size);
	memPoolNonZero(StrPools[i].pool);
    }
}

