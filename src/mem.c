
/*
 * $Id: mem.c 14623 2010-04-19 09:27:56Z adrian.chadd $
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

#include "squid.h"
#include "client_db.h"

/* module globals */

/* string pools */

/* local routines */

MemPool * pool_swap_log_data = NULL;

static void
memStringStats(StoreEntry * sentry)
{
    const char *pfmt = "%-20s\t %d\t %d\n";
    int i;
    int pooled_count = 0;
    size_t pooled_volume = 0;
    /* heading */
    storeAppendPrintf(sentry,
	"String Pool\t Impact\t\t\n"
	" \t (%%strings)\t (%%volume)\n");
    /* table body */
    for (i = 0; i < MEM_STR_POOL_COUNT; i++) {
	const MemPool *pool = StrPools[i].pool;
	const int plevel = pool->meter.inuse.level;
	storeAppendPrintf(sentry, pfmt,
	    pool->label,
	    xpercentInt(plevel, StrCountMeter.level),
	    xpercentInt(plevel * pool->obj_size, StrVolumeMeter.level));
	pooled_count += plevel;
	pooled_volume += plevel * pool->obj_size;
    }
    /* malloc strings */
    storeAppendPrintf(sentry, pfmt,
	"Other Strings",
	xpercentInt(StrCountMeter.level - pooled_count, StrCountMeter.level),
	xpercentInt(StrVolumeMeter.level - pooled_volume, StrVolumeMeter.level));
    storeAppendPrintf(sentry, "\n");
}

static void
memBufStats(StoreEntry * sentry)
{
    storeAppendPrintf(sentry, "Large buffers: %d (%d KB)\n",
	(int) HugeBufCountMeter.level,
	(int) HugeBufVolumeMeter.level / 1024);
}

static void
memStats(StoreEntry * sentry, void* data)
{
    storeBuffer(sentry);
    memReport(sentry);
    memStringStats(sentry);
    memBufStats(sentry);
    storeBufferFlush(sentry);
#if WITH_VALGRIND
    if (RUNNING_ON_VALGRIND) {
	long int leaked = 0, dubious = 0, reachable = 0, suppressed = 0;
	storeAppendPrintf(sentry, "Valgrind Report:\n");
	storeAppendPrintf(sentry, "Type\tAmount\n");
	debugs(13, 1, "Asking valgrind for memleaks");
	VALGRIND_DO_LEAK_CHECK;
	debugs(13, 1, "Getting valgrind statistics");
	VALGRIND_COUNT_LEAKS(leaked, dubious, reachable, suppressed);
	storeAppendPrintf(sentry, "Leaked\t%ld\n", leaked);
	storeAppendPrintf(sentry, "Dubious\t%ld\n", dubious);
	storeAppendPrintf(sentry, "Reachable\t%ld\n", reachable);
	storeAppendPrintf(sentry, "Suppressed\t%ld\n", suppressed);
    }
#endif
}


/*
 * public routines
 */

void
memInit(void)
{
    aclInitMem();
    authenticateInitMem();
#if USE_CACHE_DIGESTS
    peerDigestInitMem();
#endif
    disk_init_mem();
    fwdInitMem();
    httpHeaderInitMem();
    stmemInitMem();
    storeInitMem();
    netdbInitMem();
    requestInitMem();
    helperInitMem();
    clientdbInitMem();
    /* XXX this belongs in main.c nowdays */
    tlv_init();
    pool_swap_log_data = memPoolCreate("storeSwapLogData", sizeof(storeSwapLogData));
    /* Those below require conversion */
    cachemgrRegister("mem",
	"Memory Utilization",
	memStats,  NULL, NULL, 0, 1, 0);
}

void
memClean(void)
{
	memBuffersClean();
	memPoolClean();
}

