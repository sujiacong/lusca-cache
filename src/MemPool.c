
/*
 * $Id: MemPool.c 14043 2009-05-06 05:31:02Z adrian.chadd $
 *
 * DEBUG: section 63    Low Level Memory Pool Management
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


#include "squid.h"
#include "Stack.h"

/* exported */

/* module globals */

/* huge constant to set mem_idle_limit to "unlimited" */
static const size_t mem_unlimited_size = 2 * 1024 * MB - 1;

/* MemPoolMeter */

static void
memPoolMeterReport(const MemPoolMeter * pm, size_t obj_size,
    int alloc_count, int inuse_count, StoreEntry * e)
{
    assert(pm);
    storeAppendPrintf(e, "%d\t %ld\t %ld\t %.2f\t %d\t %d\t %ld\t %ld\t %d\t %ld\n",
    /* alloc */
	alloc_count,
	(long int) toKB(obj_size * pm->alloc.level),
	(long int) toKB(obj_size * pm->alloc.hwater_level),
	(double) ((squid_curtime - pm->alloc.hwater_stamp) / 3600.),
	xpercentInt(obj_size * pm->alloc.level, TheMeter.alloc.level),
    /* in use */
	inuse_count,
	(long int) toKB(obj_size * pm->inuse.level),
	(long int) toKB(obj_size * pm->inuse.hwater_level),
	xpercentInt(pm->inuse.level, pm->alloc.level),
    /* total */
	(long int) pm->total.count);
}

/* MemMeter */

/* MemPool */

#if DEBUG_MEMPOOL
static void
memPoolDiffReport(const MemPool * pool, StoreEntry * e)
{
    assert(pool);
    MemPoolMeter diff = pool->meter;
    diff.alloc.level -= pool->diff_meter.alloc.level;
    diff.inuse.level -= pool->diff_meter.inuse.level;
    diff.idle.level -= pool->diff_meter.idle.level;
    if (diff.alloc.level == 0 && diff.inuse.level == 0)
	return;
    storeAppendPrintf(e, " \t \t ");
    memPoolMeterReport(&diff, pool->obj_size,
	diff.alloc.level, pool->meter.inuse.level, e);
}
#endif

static void
memPoolReport(MemPool * pool, StoreEntry * e, int diff)
{
    assert(pool);
    storeAppendPrintf(e, "%-20s %s \t %4d\t ",
	pool->label, pool->flags.dozero ? "" : "(no-zero)", (int) pool->obj_size);
    memPoolMeterReport(&pool->meter, pool->obj_size,
	pool->meter.alloc.level, pool->meter.inuse.level, e);
#if DEBUG_MEMPOOL
    if (diff)
	memPoolDiffReport(pool, e);
    if (diff < 0)
	pool->diff_meter = pool->meter;
#endif
}

void
memReport(StoreEntry * e)
{
    size_t overhd_size = 0;
    int alloc_count = 0;
    int inuse_count = 0;
    int i;
    int diff = 0;
#if DEBUG_MEMPOOL
    char *arg = strrchr(e->mem_obj->url, '/');
    if (arg) {
	arg++;
	if (strcmp(arg, "reset") == 0)
	    diff = -1;
	else if (strcmp(arg, "diff") == 0)
	    diff = 1;
    }
    storeAppendPrintf(e, "action:mem/diff\tView diff\n");
    storeAppendPrintf(e, "action:mem/reset\tReset diff\n");
#endif
    /* caption */
    storeAppendPrintf(e, "Current memory usage:\n");
    /* heading */
    storeAppendPrintf(e, "Pool\t Obj Size\t"
	"Allocated\t\t\t\t\t In Use\t\t\t\t Hit Rate\t\n"
	" \t (bytes)\t"
	"(#)\t (KB)\t high (KB)\t high (hrs)\t impact (%%total)\t"
	"(#)\t (KB)\t high (KB)\t"
	"(%%num)\t"
	"(number)"
	"\n");
    /* main table */
    for (i = 0; i < Pools.count; i++) {
	MemPool *pool = Pools.items[i];
	if (memPoolWasUsed(pool)) {
	    memPoolReport(pool, e, diff);
	    alloc_count += pool->meter.alloc.level;
	    inuse_count += pool->meter.inuse.level;
	}
	overhd_size += sizeof(MemPool) + sizeof(MemPool *) +
	    strlen(pool->label) + 1;
    }
    overhd_size += sizeof(Pools) + Pools.capacity * sizeof(MemPool *);
    /* totals */
    storeAppendPrintf(e, "%-20s\t\t ", "Total");
    memPoolMeterReport(&TheMeter, 1, alloc_count, inuse_count, e);
    storeAppendPrintf(e, "Cumulative allocated volume: %s\n", gb_to_str(&mem_traffic_volume));
    /* overhead */
    storeAppendPrintf(e, "Current overhead: %ld bytes (%.3f%%)\n",
	(long int) overhd_size, xpercent(overhd_size, TheMeter.inuse.level));
    /* limits */
    storeAppendPrintf(e, "Idle pool limit: %.2f MB\n", toMB(mem_idle_limit));
    storeAppendPrintf(e, "memPoolAlloc calls: %d\n", MemPoolStats.alloc_calls);
    storeAppendPrintf(e, "memPoolFree calls: %d\n", MemPoolStats.free_calls);
}
