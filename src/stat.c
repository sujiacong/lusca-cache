
/*
 * $Id: stat.c 14557 2010-04-10 00:31:11Z radiant@aol.jp $
 *
 * DEBUG: section 18    Cache Manager Statistics
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

#include "../libasyncio/async_io.h"	/* For stats */
#include "../libasyncio/aiops.h"	/* For stats */

typedef int STOBJFLT(const StoreEntry *);
typedef struct {
    StoreEntry *sentry;
    int bucket;
    STOBJFLT *filter;
} StatObjectsState;

/* LOCALS */
static const char *describeStatuses(const StoreEntry *);
static const char *describeTimestamps(const StoreEntry *);
static void statAvgTick(void *notused);
static void statAvgDump(StoreEntry *, int minutes, int hours);
#if STAT_GRAPHS
static void statGraphDump(StoreEntry *);
#endif
static void statCountersInit(StatCounters *);
static void statCountersInitSpecial(StatCounters *);
static void statCountersClean(StatCounters *);
static void statCountersCopy(StatCounters * dest, const StatCounters * orig);
static double statMedianSvc(int, int);
static void statStoreEntry(MemBuf * mb, StoreEntry * e);
static double statCPUUsage(int minutes);
static OBJH stat_io_get;
static ADD AddIoStats;
static COL GetIoStats;
static OBJH stat_io_getex;
static OBJH stat_objects_get;
static OBJH stat_vmobjects_get;
static OBJH statOpenfdObj;
static EVH statObjects;
static OBJH info_get;
static ADD add_info;
static COL get_info;
static OBJH info_getex;
static OBJH statFiledescriptors;
static OBJH statCountersDump;
static COL GetCountersStats;
static ADD AddCountersStats;
static OBJH statCountersDumpEx;
static OBJH statPeerSelect;
static OBJH statDigestBlob;
static OBJH statAvg5min;
static COL getAvg5min;
static ADD addAvgStat;
static OBJH statAvg5minEx;
static OBJH statAvg60min;
static COL getAvg60min;
static OBJH statAvg60minEx;
static OBJH statUtilization;
static OBJH statCountersHistograms;
static OBJH statClientRequests;
static OBJH statCurrentStuff;
static OBJH aioStats;

#ifdef XMALLOC_STATISTICS
static void info_get_mallstat(int, int, int, void *);
static double xm_time;
static double xm_deltat;
#endif

StatCounters CountHist[N_COUNT_HIST];
static int NCountHist = 0;
static StatCounters CountHourHist[N_COUNT_HOUR_HIST];
static int NCountHourHist = 0;
CBDATA_TYPE(StatObjectsState);

static void
statUtilization(StoreEntry * e, void* data)
{
    storeAppendPrintf(e, "Cache Utilisation:\n");
    storeAppendPrintf(e, "\n");
    storeAppendPrintf(e, "Last 5 minutes:\n");
    if (NCountHist >= 5)
	statAvgDump(e, 5, 0);
    else
	storeAppendPrintf(e, "(no values recorded yet)\n");
    storeAppendPrintf(e, "\n");
    storeAppendPrintf(e, "Last 15 minutes:\n");
    if (NCountHist >= 15)
	statAvgDump(e, 15, 0);
    else
	storeAppendPrintf(e, "(no values recorded yet)\n");
    storeAppendPrintf(e, "\n");
    storeAppendPrintf(e, "Last hour:\n");
    if (NCountHist >= 60)
	statAvgDump(e, 60, 0);
    else
	storeAppendPrintf(e, "(no values recorded yet)\n");
    storeAppendPrintf(e, "\n");
    storeAppendPrintf(e, "Last 8 hours:\n");
    if (NCountHourHist >= 8)
	statAvgDump(e, 0, 8);
    else
	storeAppendPrintf(e, "(no values recorded yet)\n");
    storeAppendPrintf(e, "\n");
    storeAppendPrintf(e, "Last day:\n");
    if (NCountHourHist >= 24)
	statAvgDump(e, 0, 24);
    else
	storeAppendPrintf(e, "(no values recorded yet)\n");
    storeAppendPrintf(e, "\n");
    storeAppendPrintf(e, "Last 3 days:\n");
    if (NCountHourHist >= 72)
	statAvgDump(e, 0, 72);
    else
	storeAppendPrintf(e, "(no values recorded yet)\n");
    storeAppendPrintf(e, "\n");
    storeAppendPrintf(e, "Totals since cache startup:\n");
    statCountersDump(e,NULL);
}


int 
AddIoStats(void* A, void* B)
{
	if(!A || !B) return sizeof(IoActionData);
	int i;
	IoActionData* stats = (IoActionData*)A;
	IoActionData* statsB = (IoActionData*)B;

    stats->http_reads += statsB->http_reads;

    for (i = 0; i < 16; ++i) {
        stats->http_read_hist[i] += statsB->http_read_hist[i];
    }

    stats->ftp_reads += statsB->ftp_reads;

    for (i = 0; i < 16; ++i) {
        stats->ftp_read_hist[i] += statsB->ftp_read_hist[i];
    }

    stats->gopher_reads += statsB->gopher_reads;

    for (i = 0; i < 16; ++i) {
        stats->gopher_read_hist[i] += statsB->gopher_read_hist[i];
    }	
	return sizeof(IoActionData);
}


void* GetIoStats()
{
    IoActionData* stats = (IoActionData*)xcalloc(1,sizeof(IoActionData));
	 
    int i;

    stats->http_reads = IOStats.Http.reads;

    for (i = 0; i < 16; ++i) {
        stats->http_read_hist[i] = IOStats.Http.read_hist[i];
    }

    stats->ftp_reads = IOStats.Ftp.reads;

    for (i = 0; i < 16; ++i) {
        stats->ftp_read_hist[i] = IOStats.Ftp.read_hist[i];
    }

    stats->gopher_reads = IOStats.Gopher.reads;

    for (i = 0; i < 16; ++i) {
        stats->gopher_read_hist[i] = IOStats.Gopher.read_hist[i];
    }
    return (void*)stats;
}

void
DumpIoStats(StoreEntry* sentry, IoActionData* stats)
{
    int i;

    storeAppendPrintf(sentry, "HTTP I/O\n");
    storeAppendPrintf(sentry, "number of reads: %.0f\n", stats->http_reads);
    storeAppendPrintf(sentry, "Read Histogram:\n");

    for (i = 0; i < 16; ++i) {
        storeAppendPrintf(sentry, "%5d-%5d: %9.0f %2.0f%%\n",
                          i ? (1 << (i - 1)) + 1 : 1,
                          1 << i,
                          stats->http_read_hist[i],
                          dpercent(stats->http_read_hist[i], stats->http_reads));
    }

    storeAppendPrintf(sentry, "\n");
    storeAppendPrintf(sentry, "FTP I/O\n");
    storeAppendPrintf(sentry, "number of reads: %.0f\n", stats->ftp_reads);
    storeAppendPrintf(sentry, "Read Histogram:\n");

    for (i = 0; i < 16; ++i) {
        storeAppendPrintf(sentry, "%5d-%5d: %9.0f %2.0f%%\n",
                          i ? (1 << (i - 1)) + 1 : 1,
                          1 << i,
                          stats->ftp_read_hist[i],
                          dpercent(stats->ftp_read_hist[i], stats->ftp_reads));
    }

    storeAppendPrintf(sentry, "\n");
    storeAppendPrintf(sentry, "Gopher I/O\n");
    storeAppendPrintf(sentry, "number of reads: %.0f\n", stats->gopher_reads);
    storeAppendPrintf(sentry, "Read Histogram:\n");

    for (i = 0; i < 16; ++i) {
        storeAppendPrintf(sentry, "%5d-%5d: %9.0f %2.0f%%\n",
                          i ? (1 << (i - 1)) + 1 : 1,
                          1 << i,
                          stats->gopher_read_hist[i],
                          dpercent(stats->gopher_read_hist[i], stats->gopher_reads));
    }

    storeAppendPrintf(sentry, "\n");
}


static void
stat_io_get(StoreEntry * sentry, void* data)
{
    int i;

    storeAppendPrintf(sentry, "HTTP I/O\n");
    storeAppendPrintf(sentry, "number of reads: %d\n", IOStats.Http.reads);
    storeAppendPrintf(sentry, "Read Histogram:\n");
    for (i = 0; i < 16; i++) {
	storeAppendPrintf(sentry, "%5d-%5d: %9d %2d%%\n",
	    i ? (1 << (i - 1)) + 1 : 1,
	    1 << i,
	    IOStats.Http.read_hist[i],
	    percent(IOStats.Http.read_hist[i], IOStats.Http.reads));
    }

    storeAppendPrintf(sentry, "\n");
    storeAppendPrintf(sentry, "FTP I/O\n");
    storeAppendPrintf(sentry, "number of reads: %d\n", IOStats.Ftp.reads);
    storeAppendPrintf(sentry, "Read Histogram:\n");
    for (i = 0; i < 16; i++) {
	storeAppendPrintf(sentry, "%5d-%5d: %9d %2d%%\n",
	    i ? (1 << (i - 1)) + 1 : 1,
	    1 << i,
	    IOStats.Ftp.read_hist[i],
	    percent(IOStats.Ftp.read_hist[i], IOStats.Ftp.reads));
    }

    storeAppendPrintf(sentry, "\n");
    storeAppendPrintf(sentry, "Gopher I/O\n");
    storeAppendPrintf(sentry, "number of reads: %d\n", IOStats.Gopher.reads);
    storeAppendPrintf(sentry, "Read Histogram:\n");
    for (i = 0; i < 16; i++) {
	storeAppendPrintf(sentry, "%5d-%5d: %9d %2d%%\n",
	    i ? (1 << (i - 1)) + 1 : 1,
	    1 << i,
	    IOStats.Gopher.read_hist[i],
	    percent(IOStats.Gopher.read_hist[i], IOStats.Gopher.reads));
    }

    storeAppendPrintf(sentry, "\n");
}


static void stat_io_getex(StoreEntry * sentry, void* data)
{
	if(data)
	{
		IoActionData* stats = (IoActionData*)data;
		
		DumpIoStats(sentry,stats);
	}
	else
	{
		stat_io_get(sentry,data);
	}
}

static const char *
describeStatuses(const StoreEntry * entry)
{
    LOCAL_ARRAY(char, buf, 256);
    snprintf(buf, 256, "%-13s %-13s %-12s %-12s",
	storeStatusStr[entry->store_status],
	memStatusStr[entry->mem_status],
	swapStatusStr[entry->swap_status],
	pingStatusStr[entry->ping_status]);
    return buf;
}

const char *
storeEntryFlags(const StoreEntry * entry)
{
    LOCAL_ARRAY(char, buf, 256);
    int flags = (int) entry->flags;
    char *t;
    buf[0] = '\0';
    if (EBIT_TEST(flags, ENTRY_SPECIAL))
	strcat(buf, "SPECIAL,");
    if (EBIT_TEST(flags, ENTRY_REVALIDATE))
	strcat(buf, "REVALIDATE,");
    if (EBIT_TEST(flags, DELAY_SENDING))
	strcat(buf, "DELAY_SENDING,");
    if (EBIT_TEST(flags, RELEASE_REQUEST))
	strcat(buf, "RELEASE_REQUEST,");
    if (EBIT_TEST(flags, REFRESH_FAILURE))
	strcat(buf, "REFRESH_FAILURE,");
    if (EBIT_TEST(flags, ENTRY_CACHABLE))
	strcat(buf, "CACHABLE,");
    if (EBIT_TEST(flags, ENTRY_DISPATCHED))
	strcat(buf, "DISPATCHED,");
    if (EBIT_TEST(flags, KEY_PRIVATE))
	strcat(buf, "PRIVATE,");
    if (EBIT_TEST(flags, ENTRY_FWD_HDR_WAIT))
	strcat(buf, "FWD_HDR_WAIT,");
    if (EBIT_TEST(flags, ENTRY_NEGCACHED))
	strcat(buf, "NEGCACHED,");
    if (EBIT_TEST(flags, ENTRY_VALIDATED))
	strcat(buf, "VALIDATED,");
    if (EBIT_TEST(flags, ENTRY_BAD_LENGTH))
	strcat(buf, "BAD_LENGTH,");
    if (EBIT_TEST(flags, ENTRY_ABORTED))
	strcat(buf, "ABORTED,");
    if ((t = strrchr(buf, ',')))
	*t = '\0';
    return buf;
}

static const char *
describeTimestamps(const StoreEntry * entry)
{
    LOCAL_ARRAY(char, buf, 256);
    snprintf(buf, 256, "LV:%-9d LU:%-9d LM:%-9d EX:%-9d",
	(int) entry->timestamp,
	(int) entry->lastref,
	(int) entry->lastmod,
	(int) entry->expires);
    return buf;
}

static void
statStoreEntry(MemBuf * mb, StoreEntry * e)
{
    MemObject *mem = e->mem_obj;
    int i;
    struct _store_client *sc;
    dlink_node *node;
    memBufPrintf(mb, "KEY %s\n", storeKeyText(e->hash.key));
    /* XXX should this url be escaped? */
    if (mem)
	memBufPrintf(mb, "\t%s %s\n", urlMethodGetConstStr(mem->method), mem->url);
    if (mem && mem->store_url)
	memBufPrintf(mb, "\tStore lookup URL: %s\n", mem->store_url);
    memBufPrintf(mb, "\t%s\n", describeStatuses(e));
    memBufPrintf(mb, "\t%s\n", storeEntryFlags(e));
    memBufPrintf(mb, "\t%s\n", describeTimestamps(e));
    memBufPrintf(mb, "\t%d locks, %d clients, %d refs\n",
	(int) e->lock_count,
	storePendingNClients(e),
	(int) e->refcount);
    memBufPrintf(mb, "\tSwap Dir %d, File %#08X\n",
	e->swap_dirn, e->swap_filen);
    if (mem != NULL) {
	memBufPrintf(mb, "\tinmem_lo: %" PRINTF_OFF_T "\n", mem->inmem_lo);
	memBufPrintf(mb, "\tinmem_hi: %" PRINTF_OFF_T "\n", mem->inmem_hi);
	memBufPrintf(mb, "\tswapout: %" PRINTF_OFF_T " bytes queued\n",
	    mem->swapout.queue_offset);
	if (mem->swapout.sio)
	    memBufPrintf(mb, "\tswapout: %" PRINTF_OFF_T " bytes written\n",
		storeOffset(mem->swapout.sio));
	for (i = 0, node = mem->clients.head; node; node = node->next, i++) {
	    sc = (store_client *) node->data;
	    memBufPrintf(mb, "\tClient #%d, %p\n", i, sc->callback_data);
	    memBufPrintf(mb, "\t\tcopy_offset: %" PRINTF_OFF_T "\n",
		sc->copy_offset);
	    memBufPrintf(mb, "\t\tseen_offset: %" PRINTF_OFF_T "\n",
		sc->seen_offset);
	    memBufPrintf(mb, "\t\tcopy_size: %d\n",
		(int) sc->copy_size);
	    memBufPrintf(mb, "\t\tflags:");
	    if (sc->flags.disk_io_pending)
		memBufPrintf(mb, " disk_io_pending");
	    if (sc->flags.store_copying)
		memBufPrintf(mb, " store_copying");
	    if (sc->flags.copy_event_pending)
		memBufPrintf(mb, " copy_event_pending");
	    memBufPrintf(mb, "\n");
	}
    }
    memBufPrintf(mb, "\n");
}

/* process objects list */
static void
statObjects(void *data)
{
    StatObjectsState *state = data;
    StoreEntry *e;
    hash_link *link_ptr = NULL;
    hash_link *link_next = NULL;
    if (state->bucket >= store_hash_buckets) {
	storeComplete(state->sentry);
	storeUnlockObject(state->sentry);
	cbdataFree(state);
	return;
    } else if (EBIT_TEST(state->sentry->flags, ENTRY_ABORTED)) {
	storeUnlockObject(state->sentry);
	cbdataFree(state);
	return;
    } else if (fwdCheckDeferRead(-1, state->sentry)) {
	eventAdd("statObjects", statObjects, state, 0.1, 1);
	return;
    }
    debugs(49, 3, "statObjects: Bucket #%d", state->bucket);
    link_next = hash_get_bucket(store_table, state->bucket);
    if (link_next) {
	MemBuf mb;
	memBufDefInit(&mb);
	while (NULL != (link_ptr = link_next)) {
	    link_next = link_ptr->next;
	    e = (StoreEntry *) link_ptr;
	    if (state->filter && 0 == state->filter(e))
		continue;
	    statStoreEntry(&mb, e);
	}
	storeAppend(state->sentry, mb.buf, mb.size);
	memBufClean(&mb);
    }
    state->bucket++;
    eventAdd("statObjects", statObjects, state, 0.0, 1);
}

static void
statObjectsStart(StoreEntry * sentry, STOBJFLT * filter)
{
    StatObjectsState *state;
    state = cbdataAlloc(StatObjectsState);
    state->sentry = sentry;
    state->filter = filter;
    storeLockObject(sentry);
    eventAdd("statObjects", statObjects, state, 0.0, 1);
}

static void
stat_objects_get(StoreEntry * sentry, void* data)
{
    statObjectsStart(sentry, NULL);
}

static int
statObjectsVmFilter(const StoreEntry * e)
{
    return e->mem_obj ? 1 : 0;
}

static void
stat_vmobjects_get(StoreEntry * sentry, void* data)
{
    statObjectsStart(sentry, statObjectsVmFilter);
}

static int
statObjectsOpenfdFilter(const StoreEntry * e)
{
    if (e->mem_obj == NULL)
	return 0;
    if (e->mem_obj->swapout.sio == NULL)
	return 0;
    return 1;
}

static void
statOpenfdObj(StoreEntry * sentry, void* data)
{
    statObjectsStart(sentry, statObjectsOpenfdFilter);
}

static int
statObjectsPendingFilter(const StoreEntry * e)
{
    if (e->store_status != STORE_PENDING)
	return 0;
    return 1;
}

static void
statPendingObj(StoreEntry * sentry, void* data)
{
    statObjectsStart(sentry, statObjectsPendingFilter);
}

static int
statObjectsClientsFilter(const StoreEntry * e)
{
    if (e->mem_obj == NULL)
	return 0;
    if (e->mem_obj->clients.head == NULL)
	return 0;
    return 1;
}

static void
statClientsObj(StoreEntry * sentry, void* data)
{
    statObjectsStart(sentry, statObjectsClientsFilter);
}

#ifdef XMALLOC_STATISTICS
static void
info_get_mallstat(int size, int number, int oldnum, void *data)
{
    StoreEntry *sentry = data;
    if (number > 0)
	storeAppendPrintf(sentry, "%d\t %d\t %d\t %.1f\n", size, number, number - oldnum, xdiv((number - oldnum), xm_deltat));
}
#endif

static const char *
fdRemoteAddr(const fde * f)
{
    LOCAL_ARRAY(char, buf, SQUIDHOSTNAMELEN);
    if (f->type != FD_SOCKET)
	return null_string;
    if (*f->ipaddrstr)
	snprintf(buf, SQUIDHOSTNAMELEN, "%s.%d", f->ipaddrstr, (int) f->remote_port);
    else {
	if (!sqinet_is_anyaddr(&f->local_address)) {
	    char s[MAX_IPSTRLEN];
	    sqinet_ntoa(&f->local_address, s, sizeof(s), 0);
	    snprintf(buf, SQUIDHOSTNAMELEN, "%s.%d", s, (int) f->local_port);
	} else {
	    snprintf(buf, SQUIDHOSTNAMELEN, "*.%d", (int) f->local_port);
	}
    }
    return buf;
}

static void
statFiledescriptors(StoreEntry * sentry, void* data)
{
    int i;
    fde *f;
    storeAppendPrintf(sentry, "Active file descriptors:\n");
#ifdef _SQUID_MSWIN_
    storeAppendPrintf(sentry, "%-4s %-10s %-6s %-4s %-7s* %-7s* %-21s %s\n",
	"File",
	"Handle",
#else
    storeAppendPrintf(sentry, "%-4s %-6s %-4s %-7s* %-7s* %-21s %s\n",
	"File",
#endif
	"Type",
	"Tout",
	"Nread",
	"Nwrite",
	"Remote Address",
	"Description");
#ifdef _SQUID_MSWIN_
    storeAppendPrintf(sentry, "---- ---------- ------ ---- -------- -------- --------------------- ------------------------------\n");
#else
    storeAppendPrintf(sentry, "---- ------ ---- -------- -------- --------------------- ------------------------------\n");
#endif
    for (i = 0; i < Squid_MaxFD; i++) {
	f = &fd_table[i];
	if (!f->flags.open)
	    continue;
#ifdef _SQUID_MSWIN_
	storeAppendPrintf(sentry, "%4d 0x%-8lX %-6.6s %4d %7" PRINTF_OFF_T "%c %7" PRINTF_OFF_T "%c %-21s %s\n",
	    i,
	    f->win32.handle,
#else
	storeAppendPrintf(sentry, "%4d %-6.6s %4d %7" PRINTF_OFF_T "%c %7" PRINTF_OFF_T "%c %-21s %s\n",
	    i,
#endif
	    fdTypeStr[f->type],
	    f->timeout_handler ? (int) (f->timeout - squid_curtime) : 0,
	    f->bytes_read,
	    f->read_handler ? '*' : ' ',
	    f->bytes_written,
	    f->write_handler ? '*' : ' ',
	    fdRemoteAddr(f),
	    f->desc);
    }
}

int add_info(void* A, void* B)
{
	if(!A || !B) return sizeof(InfoActionData);
	
	InfoActionData* stats = (InfoActionData*)A;
	InfoActionData* statsB = (InfoActionData*)B;
	
    if (!timerisset(&squid_start) || timercmp(&squid_start, &statsB->squid_start, >))
        stats->squid_start = statsB->squid_start;
    if (timercmp(&current_time, &statsB->current_time, <))
        stats->current_time = statsB->current_time;
	
	stats->client_http_clients	+= statsB->client_http_clients;
	stats->client_http_requests  += statsB->client_http_requests;
	stats->icp_pkts_recv  += statsB->icp_pkts_recv;
	stats->icp_pkts_sent  += statsB->icp_pkts_sent;
	stats->icp_replies_queued  += statsB->icp_replies_queued;
#if USE_HTCP
	stats->htcp_pkts_recv  += statsB->htcp_pkts_recv;
	stats->htcp_pkts_sent  += statsB->htcp_pkts_sent;
#endif
	stats->request_failure_ratio  += statsB->request_failure_ratio;
	stats->avg_client_http_requests  += statsB->avg_client_http_requests;
	stats->avg_icp_messages  += statsB->avg_icp_messages;
	stats->select_loops  += statsB->select_loops;
	stats->avg_loop_time  += statsB->avg_loop_time;
	stats->request_hit_ratio5  += statsB->request_hit_ratio5;
	stats->request_hit_ratio60	+= statsB->request_hit_ratio60;
	stats->byte_hit_ratio5	+= statsB->byte_hit_ratio5;
	stats->byte_hit_ratio60  += statsB->byte_hit_ratio60;
	stats->request_hit_mem_ratio5  += statsB->request_hit_mem_ratio5;
	stats->request_hit_mem_ratio60	+= statsB->request_hit_mem_ratio60;
	stats->request_hit_disk_ratio5	+= statsB->request_hit_disk_ratio5;
	stats->request_hit_disk_ratio60  += statsB->request_hit_disk_ratio60;
	stats->storeswapsize  +=	statsB->storeswapsize;
	stats->storememsize  += 	statsB->storememsize;
	stats->meanobjectsize  +=	statsB->meanobjectsize;
	stats->unlink_requests	+= statsB->unlink_requests;
	stats->http_requests5  += statsB->http_requests5;
	stats->http_requests60	+= statsB->http_requests60;
	stats->cache_misses5  += statsB->cache_misses5;
	stats->cache_misses60  += statsB->cache_misses60;
	stats->cache_hits5	+= statsB->cache_hits5;
	stats->cache_hits60  += statsB->cache_hits60;
	stats->near_hits5  += statsB->near_hits5;
	stats->near_hits60	+= statsB->near_hits60;
	stats->not_modified_replies5  += statsB->not_modified_replies5;
	stats->not_modified_replies60  += statsB->not_modified_replies60;
	stats->dns_lookups5  += statsB->dns_lookups5;
	stats->dns_lookups60  += statsB->dns_lookups60;
	stats->icp_queries5  += statsB->icp_queries5;
	stats->icp_queries60  += statsB->icp_queries60;
	
    if (statsB->up_time > stats->up_time)
        stats->up_time = statsB->up_time;

	stats->cpu_time  += statsB->cpu_time;
	stats->cpu_usage  += statsB->cpu_usage;
	stats->cpu_usage5  += statsB->cpu_usage5;
	stats->cpu_usage60	+= statsB->cpu_usage60;
	stats->maxrss  += statsB->maxrss;
	stats->page_faults	+= statsB->page_faults;
#if HAVE_MSTATS && HAVE_GNUMALLOC_H
	ms	+=	   ms;
	stats->ms_bytes_total  += statsB->ms_bytes_total;
	stats->ms_bytes_free  += statsB->ms_bytes_free;
	stats->ms_free_percent	+=	statsB->ms_free_percent;
#endif
	stats->total_accounted	+= statsB->total_accounted;
	stats->alloc_calls	+= statsB->alloc_calls;
	stats->total_accounted  += statsB->total_accounted;
	stats->free_calls  += statsB->free_calls;
	stats->max_fd  += statsB->max_fd;
	stats->biggest_fd  += statsB->biggest_fd;
	stats->number_fd  += statsB->number_fd;
	stats->opening_fd  += statsB->opening_fd;
	stats->num_fd_free	+= statsB->num_fd_free;
	stats->reserved_fd	+= statsB->reserved_fd;
	stats->open_disk_fd  += statsB->open_disk_fd;

	stats->store_entry_count  +=	stats->store_entry_count;
	stats->store_mem_object_count  +=	stats->store_mem_object_count;
	stats->store_mem_count	+=	stats->store_mem_count;
	stats->store_swap_count   +=	stats->store_swap_count;

	++stats->count;

	return sizeof(InfoActionData);
}

void* get_info()
{
	InfoActionData* stats = xcalloc(1, sizeof(InfoActionData));
    struct rusage rusage;
    double cputime;
    double runtime;
#if HAVE_MSTATS && HAVE_GNUMALLOC_H
    struct mstats ms;
#endif

    runtime = tvSubDsec(squid_start, current_time);

    if (runtime == 0.0)
        runtime = 1.0;

    stats->squid_start = squid_start;

    stats->current_time = current_time;

    stats->client_http_clients = statCounter.client_http.clients;

    stats->client_http_requests = statCounter.client_http.requests;

    stats->icp_pkts_recv = statCounter.icp.pkts_recv;

    stats->icp_pkts_sent = statCounter.icp.pkts_sent;

    stats->icp_replies_queued = statCounter.icp.replies_queued;

#if USE_HTCP

    stats->htcp_pkts_recv = statCounter.htcp.pkts_recv;

    stats->htcp_pkts_sent = statCounter.htcp.pkts_sent;

#endif

    stats->request_failure_ratio = request_failure_ratio;

    stats->avg_client_http_requests = statCounter.client_http.requests / (runtime / 60.0);

    stats->avg_icp_messages = (statCounter.icp.pkts_sent + statCounter.icp.pkts_recv) / (runtime / 60.0);

    stats->select_loops = CommStats.select_loops;
    stats->avg_loop_time = 1000.0 * runtime / CommStats.select_loops;

    stats->request_hit_ratio5 = statRequestHitRatio(5);
    stats->request_hit_ratio60 = statRequestHitRatio(60);

    stats->byte_hit_ratio5 = statByteHitRatio(5);
    stats->byte_hit_ratio60 = statByteHitRatio(60);

    stats->request_hit_mem_ratio5 = statRequestHitMemoryRatio(5);
    stats->request_hit_mem_ratio60 = statRequestHitMemoryRatio(60);

    stats->request_hit_disk_ratio5 = statRequestHitDiskRatio(5);
    stats->request_hit_disk_ratio60 = statRequestHitDiskRatio(60);

	stats->storeswapsize = store_swap_size;
	stats->storememsize = store_mem_size;
	stats->meanobjectsize = (n_disk_objects ? (double) store_swap_size / n_disk_objects : 0.0);
    stats->unlink_requests = statCounter.unlink.requests;

    stats->http_requests5 = statMedianSvc(5, MEDIAN_HTTP);
    stats->http_requests60 = statMedianSvc(60, MEDIAN_HTTP);

    stats->cache_misses5 = statMedianSvc(5, MEDIAN_MISS);
    stats->cache_misses60 = statMedianSvc(60, MEDIAN_MISS);

    stats->cache_hits5 = statMedianSvc(5, MEDIAN_HIT);
    stats->cache_hits60 = statMedianSvc(60, MEDIAN_HIT);

    stats->near_hits5 = statMedianSvc(5, MEDIAN_NH);
    stats->near_hits60 = statMedianSvc(60, MEDIAN_NH);

    stats->not_modified_replies5 = statMedianSvc(5, MEDIAN_NM);
    stats->not_modified_replies60 = statMedianSvc(60, MEDIAN_NM);

    stats->dns_lookups5 = statMedianSvc(5, MEDIAN_DNS);
    stats->dns_lookups60 = statMedianSvc(60, MEDIAN_DNS);

    stats->icp_queries5 = statMedianSvc(5, MEDIAN_ICP_QUERY);
    stats->icp_queries60 = statMedianSvc(60, MEDIAN_ICP_QUERY);

    squid_getrusage(&rusage);
    cputime = rusage_cputime(&rusage);

    stats->up_time = runtime;
    stats->cpu_time = cputime;
    stats->cpu_usage = dpercent(cputime, runtime);
    stats->cpu_usage5 = statCPUUsage(5);
    stats->cpu_usage60 = statCPUUsage(60);

    stats->maxrss = rusage_maxrss(&rusage);
    stats->page_faults = rusage_pagefaults(&rusage);

#if HAVE_MSTATS && HAVE_GNUMALLOC_H
    ms = mstats();
    stats->ms_bytes_total = (double) (ms.bytes_total);
    stats->ms_bytes_free = (double) (ms.bytes_free);
	stats->ms_free_percent = percent(ms.bytes_free, ms.bytes_total);
#endif
    stats->total_accounted = statMemoryAccounted();
    stats->alloc_calls = MemPoolStats.alloc_calls;
    stats->free_calls = MemPoolStats.free_calls;

    stats->max_fd = Squid_MaxFD;
    stats->biggest_fd = Biggest_FD;
    stats->number_fd = Number_FD;
    stats->opening_fd = Opening_FD;
    stats->num_fd_free = fdNFree();
    stats->reserved_fd = RESERVED_FD;

    stats->open_disk_fd = store_open_disk_fd;
	
	stats->store_entry_count = memPoolInUseCount(pool_storeentry);
	stats->store_mem_object_count = memPoolInUseCount(pool_memobject);
	stats->store_mem_count = hot_obj_count;
	stats->store_swap_count  = n_disk_objects;

	stats->count = 1;
	
	return (void*)stats;
}


static void
DumpInfo(StoreEntry * sentry, void* data)
{
	InfoActionData* stats = (InfoActionData*)data;
	
	storeAppendPrintf(sentry, "Squid Object Cache: Version %s\n",
					  version_string);
#if _SQUID_WIN32_
    if (WIN32_run_mode == _WIN_SQUID_RUN_MODE_SERVICE) {
	storeAppendPrintf(sentry, "\nRunning as %s Windows System Service on %s\n",
	    WIN32_Service_name, WIN32_OS_string);
	storeAppendPrintf(sentry, "Service command line is: %s\n", WIN32_Service_Command_Line);
    } else
	storeAppendPrintf(sentry, "Running on %s\n", WIN32_OS_string);
#endif

	storeAppendPrintf(sentry, "Start Time:\t%s\n",
					  mkrfc1123(stats->squid_start.tv_sec));

	storeAppendPrintf(sentry, "Current Time:\t%s\n",
					  mkrfc1123(stats->current_time.tv_sec));

	storeAppendPrintf(sentry, "Connection information for %s:\n",APP_SHORTNAME);

	if (Config.onoff.client_db)
		storeAppendPrintf(sentry, "\tNumber of clients accessing cache:\t%.0f\n", stats->client_http_clients);
	else
		storeAppendPrintf(sentry,"%s","\tNumber of clients accessing cache:\t(client_db off)\n");

	storeAppendPrintf(sentry, "\tNumber of HTTP requests received:\t%.0f\n",
					  stats->client_http_requests);

	storeAppendPrintf(sentry, "\tNumber of ICP messages received:\t%.0f\n",
					  stats->icp_pkts_recv);

	storeAppendPrintf(sentry, "\tNumber of ICP messages sent:\t%.0f\n",
					  stats->icp_pkts_sent);

	storeAppendPrintf(sentry, "\tNumber of queued ICP replies:\t%.0f\n",
					  stats->icp_replies_queued);

#if USE_HTCP

	storeAppendPrintf(sentry, "\tNumber of HTCP messages received:\t%.0f\n",
					  stats->htcp_pkts_recv);

	storeAppendPrintf(sentry, "\tNumber of HTCP messages sent:\t%.0f\n",
					  stats->htcp_pkts_sent);

#endif

	double fct = stats->count > 1 ? stats->count : 1.0;
	storeAppendPrintf(sentry, "\tRequest failure ratio:\t%5.2f\n",
					  stats->request_failure_ratio / fct);

	storeAppendPrintf(sentry, "\tAverage HTTP requests per minute since start:\t%.1f\n",
					  stats->avg_client_http_requests);

	storeAppendPrintf(sentry, "\tAverage ICP messages per minute since start:\t%.1f\n",
					  stats->avg_icp_messages);

	storeAppendPrintf(sentry, "\tSelect loop called: %.0f times, %0.3f ms avg\n",
					  stats->select_loops, stats->avg_loop_time / fct);

	storeAppendPrintf(sentry, "Cache information for %s:\n",APP_SHORTNAME);

	storeAppendPrintf(sentry, "\tHits as %% of all requests:\t5min: %3.1f%%, 60min: %3.1f%%\n",
					  stats->request_hit_ratio5 / fct,
					  stats->request_hit_ratio60 / fct);

	storeAppendPrintf(sentry, "\tHits as %% of bytes sent:\t5min: %3.1f%%, 60min: %3.1f%%\n",
					  stats->byte_hit_ratio5 / fct,
					  stats->byte_hit_ratio60 / fct);

	storeAppendPrintf(sentry, "\tMemory hits as %% of hit requests:\t5min: %3.1f%%, 60min: %3.1f%%\n",
					  stats->request_hit_mem_ratio5 / fct,
					  stats->request_hit_mem_ratio60 / fct);

	storeAppendPrintf(sentry, "\tDisk hits as %% of hit requests:\t5min: %3.1f%%, 60min: %3.1f%%\n",
					  stats->request_hit_disk_ratio5 / fct,
					  stats->request_hit_disk_ratio60 / fct);

	storeAppendPrintf(sentry, "\tStorage Swap size:\t%.0f KB\n",
					  stats->storeswapsize / 1024);
	storeAppendPrintf(sentry, "\tStorage Mem size:\t%.0f KB\n",
					  stats->storememsize / 1024);
	storeAppendPrintf(sentry, "\tMean Object Size:\t%0.2f KB\n",
					  stats->meanobjectsize / 1024);
	storeAppendPrintf(sentry, "\tRequests given to unlinkd:\t%.0f\n",
					  stats->unlink_requests);

	storeAppendPrintf(sentry, "Median Service Times (seconds)  5 min	60 min:\n");
	fct = stats->count > 1 ? stats->count * 1000.0 : 1000.0;
	storeAppendPrintf(sentry, "\tHTTP Requests (All):  %8.5f %8.5f\n",
					  stats->http_requests5 / fct,
					  stats->http_requests60 / fct);

	storeAppendPrintf(sentry, "\tCache Misses:		   %8.5f %8.5f\n",
					  stats->cache_misses5 / fct,
					  stats->cache_misses60 / fct);

	storeAppendPrintf(sentry, "\tCache Hits:		   %8.5f %8.5f\n",
					  stats->cache_hits5 / fct,
					  stats->cache_hits60 / fct);

	storeAppendPrintf(sentry, "\tNear Hits: 		   %8.5f %8.5f\n",
					  stats->near_hits5 / fct,
					  stats->near_hits60 / fct);

	storeAppendPrintf(sentry, "\tNot-Modified Replies: %8.5f %8.5f\n",
					  stats->not_modified_replies5 / fct,
					  stats->not_modified_replies60 / fct);

	storeAppendPrintf(sentry, "\tDNS Lookups:		   %8.5f %8.5f\n",
					  stats->dns_lookups5 / fct,
					  stats->dns_lookups60 / fct);

	fct = stats->count > 1 ? stats->count * 1000000.0 : 1000000.0;
	storeAppendPrintf(sentry, "\tICP Queries:		   %8.5f %8.5f\n",
					  stats->icp_queries5 / fct,
					  stats->icp_queries60 / fct);

	storeAppendPrintf(sentry, "Resource usage for %s:\n", APP_SHORTNAME);

	storeAppendPrintf(sentry, "\tUP Time:\t%.3f seconds\n", stats->up_time);

	storeAppendPrintf(sentry, "\tCPU Time:\t%.3f seconds\n", stats->cpu_time);

	storeAppendPrintf(sentry, "\tCPU Usage:\t%.2f%%\n",
					  stats->cpu_usage);

	storeAppendPrintf(sentry, "\tCPU Usage, 5 minute avg:\t%.2f%%\n",
					  stats->cpu_usage5);

	storeAppendPrintf(sentry, "\tCPU Usage, 60 minute avg:\t%.2f%%\n",
					  stats->cpu_usage60);

	storeAppendPrintf(sentry, "\tMaximum Resident Size: %.0f KB\n",
					  stats->maxrss);

	storeAppendPrintf(sentry, "\tPage faults with physical i/o: %.0f\n",
					  stats->page_faults);

#if HAVE_MSTATS && HAVE_GNUMALLOC_H

	storeAppendPrintf(sentry, "Memory usage for %s via mstats():\n",APP_SHORTNAME);

	storeAppendPrintf(sentry, "\tTotal space in arena:	%6.0f KB\n",
					  stats->ms_bytes_total / 1024);

	storeAppendPrintf(sentry, "\tTotal free:			%6.0f KB %.0f%%\n",
					  stats->ms_bytes_free / 1024,
					  stats->ms_free_percent);

#endif

	storeAppendPrintf(sentry, "Memory accounted for:\n");
	storeAppendPrintf(sentry, "\tTotal accounted:		%6.0f KB\n",
					  stats->total_accounted / 1024);
	
	storeAppendPrintf(sentry, "\tmemPoolAlloc calls: %9.0f\n",
					  stats->alloc_calls);
	storeAppendPrintf(sentry, "\tmemPoolFree calls:  %9.0f\n",
					  stats->free_calls);
	
	storeAppendPrintf(sentry, "File descriptor usage for %s:\n", APP_SHORTNAME);
	storeAppendPrintf(sentry, "\tMaximum number of file descriptors:   %4.0f\n",
					  stats->max_fd);
	storeAppendPrintf(sentry, "\tLargest file desc currently in use:   %4.0f\n",
					  stats->biggest_fd);
	storeAppendPrintf(sentry, "\tNumber of file desc currently in use: %4.0f\n",
					  stats->number_fd);
	storeAppendPrintf(sentry, "\tFiles queued for open: 			   %4.0f\n",
					  stats->opening_fd);
	storeAppendPrintf(sentry, "\tAvailable number of file descriptors: %4.0f\n",
					  stats->num_fd_free);
	storeAppendPrintf(sentry, "\tReserved number of file descriptors:  %4.0f\n",
					  stats->reserved_fd);
	storeAppendPrintf(sentry, "\tStore Disk files open: 			   %4.0f\n",
					  stats->open_disk_fd);

	storeAppendPrintf(sentry, "Internal Data Structures:\n");
	storeAppendPrintf(sentry, "\t%6.0f StoreEntries\n",
					  stats->store_entry_count);
	storeAppendPrintf(sentry, "\t%6.0f StoreEntries with MemObjects\n",
					  stats->store_mem_object_count);
	storeAppendPrintf(sentry, "\t%6.0f Hot Object Cache Items\n",
					  stats->store_mem_count);
	storeAppendPrintf(sentry, "\t%6.0f on-disk objects\n",
					  stats->store_swap_count);

}

static void
info_get(StoreEntry * sentry, void* data)
{
    struct rusage rusage;
    double cputime;
    double runtime;
#if HAVE_MSTATS && HAVE_GNUMALLOC_H
    struct mstats ms;
#elif HAVE_MALLINFO && HAVE_STRUCT_MALLINFO
    struct mallinfo mp;
    int t;
#endif

    runtime = tvSubDsec(squid_start, current_time);
    if (runtime == 0.0)
	runtime = 1.0;
    storeAppendPrintf(sentry, "Squid Object Cache: Version %s\n",
	version_string);
#ifdef _SQUID_WIN32_
    if (WIN32_run_mode == _WIN_SQUID_RUN_MODE_SERVICE) {
	storeAppendPrintf(sentry, "\nRunning as %s Windows System Service on %s\n",
	    WIN32_Service_name, WIN32_OS_string);
	storeAppendPrintf(sentry, "Service command line is: %s\n", WIN32_Service_Command_Line);
    } else
	storeAppendPrintf(sentry, "Running on %s\n", WIN32_OS_string);
#endif
    storeAppendPrintf(sentry, "Start Time:\t%s\n",
	mkrfc1123(squid_start.tv_sec));
    storeAppendPrintf(sentry, "Current Time:\t%s\n",
	mkrfc1123(current_time.tv_sec));
    storeAppendPrintf(sentry, "Connection information for %s:\n",
	appname);
    storeAppendPrintf(sentry, "\tNumber of clients accessing cache:\t%u\n",
	statCounter.client_http.clients);
    storeAppendPrintf(sentry, "\tNumber of HTTP requests received:\t%u\n",
	statCounter.client_http.requests);
    storeAppendPrintf(sentry, "\tNumber of ICP messages received:\t%u\n",
	statCounter.icp.pkts_recv);
    storeAppendPrintf(sentry, "\tNumber of ICP messages sent:\t%u\n",
	statCounter.icp.pkts_sent);
    storeAppendPrintf(sentry, "\tNumber of queued ICP replies:\t%u\n",
	statCounter.icp.replies_queued);
#if USE_HTCP
    storeAppendPrintf(sentry, "\tNumber of HTCP messages received:\t%u\n",
	statCounter.htcp.pkts_recv);
    storeAppendPrintf(sentry, "\tNumber of HTCP messages sent:\t%u\n",
	statCounter.htcp.pkts_sent);
#endif
    storeAppendPrintf(sentry, "\tRequest failure ratio:\t%5.2f\n",
	request_failure_ratio);

    storeAppendPrintf(sentry, "\tAverage HTTP requests per minute since start:\t%.1f\n",
	statCounter.client_http.requests / (runtime / 60.0));
    storeAppendPrintf(sentry, "\tAverage ICP messages per minute since start:\t%.1f\n",
	(statCounter.icp.pkts_sent + statCounter.icp.pkts_recv) / (runtime / 60.0));

    storeAppendPrintf(sentry, "\tSelect loop called: %d times, %0.3f ms avg\n",
	CommStats.select_loops, 1000.0 * runtime / CommStats.select_loops);

    storeAppendPrintf(sentry, "Cache information for %s:\n",
	appname);
    storeAppendPrintf(sentry, "\tRequest Hit Ratios:\t5min: %3.1f%%, 60min: %3.1f%%\n",
	statRequestHitRatio(5),
	statRequestHitRatio(60));
    storeAppendPrintf(sentry, "\tByte Hit Ratios:\t5min: %3.1f%%, 60min: %3.1f%%\n",
	statByteHitRatio(5),
	statByteHitRatio(60));
    storeAppendPrintf(sentry, "\tRequest Memory Hit Ratios:\t5min: %3.1f%%, 60min: %3.1f%%\n",
	statRequestHitMemoryRatio(5),
	statRequestHitMemoryRatio(60));
    storeAppendPrintf(sentry, "\tRequest Disk Hit Ratios:\t5min: %3.1f%%, 60min: %3.1f%%\n",
	statRequestHitDiskRatio(5),
	statRequestHitDiskRatio(60));
	
    storeAppendPrintf(sentry, "\tStorage Swap size:\t%d KB\n",
	store_swap_size);
    storeAppendPrintf(sentry, "\tStorage Mem size:\t%d KB\n",
	(int) (store_mem_size >> 10));
    storeAppendPrintf(sentry, "\tMean Object Size:\t%0.2f KB\n",
	n_disk_objects ? (double) store_swap_size / n_disk_objects : 0.0);
    storeAppendPrintf(sentry, "\tRequests given to unlinkd:\t%d\n",
	statCounter.unlink.requests);

    storeAppendPrintf(sentry, "Median Service Times (seconds)  5 min    60 min:\n");
    storeAppendPrintf(sentry, "\tHTTP Requests (All):  %8.5f %8.5f\n",
	statMedianSvc(5, MEDIAN_HTTP) / 1000.0,
	statMedianSvc(60, MEDIAN_HTTP) / 1000.0);
    storeAppendPrintf(sentry, "\tCache Misses:         %8.5f %8.5f\n",
	statMedianSvc(5, MEDIAN_MISS) / 1000.0,
	statMedianSvc(60, MEDIAN_MISS) / 1000.0);
    storeAppendPrintf(sentry, "\tCache Hits:           %8.5f %8.5f\n",
	statMedianSvc(5, MEDIAN_HIT) / 1000.0,
	statMedianSvc(60, MEDIAN_HIT) / 1000.0);
    storeAppendPrintf(sentry, "\tNear Hits:            %8.5f %8.5f\n",
	statMedianSvc(5, MEDIAN_NH) / 1000.0,
	statMedianSvc(60, MEDIAN_NH) / 1000.0);
    storeAppendPrintf(sentry, "\tNot-Modified Replies: %8.5f %8.5f\n",
	statMedianSvc(5, MEDIAN_NM) / 1000.0,
	statMedianSvc(60, MEDIAN_NM) / 1000.0);
    storeAppendPrintf(sentry, "\tDNS Lookups:          %8.5f %8.5f\n",
	statMedianSvc(5, MEDIAN_DNS) / 1000.0,
	statMedianSvc(60, MEDIAN_DNS) / 1000.0);
    storeAppendPrintf(sentry, "\tICP Queries:          %8.5f %8.5f\n",
	statMedianSvc(5, MEDIAN_ICP_QUERY) / 1000000.0,
	statMedianSvc(60, MEDIAN_ICP_QUERY) / 1000000.0);

    squid_getrusage(&rusage);
    cputime = rusage_cputime(&rusage);
    storeAppendPrintf(sentry, "Resource usage for %s:\n", appname);
    storeAppendPrintf(sentry, "\tUP Time:\t%.3f seconds\n", runtime);
    storeAppendPrintf(sentry, "\tCPU Time:\t%.3f seconds\n", cputime);
    storeAppendPrintf(sentry, "\tCPU Usage:\t%.2f%%\n",
	dpercent(cputime, runtime));
    storeAppendPrintf(sentry, "\tCPU Usage, 5 minute avg:\t%.2f%%\n",
	statCPUUsage(5));
    storeAppendPrintf(sentry, "\tCPU Usage, 60 minute avg:\t%.2f%%\n",
	statCPUUsage(60));
#if HAVE_SBRK
    storeAppendPrintf(sentry, "\tProcess Data Segment Size via sbrk(): %lu KB\n",
	(unsigned long) (((char *) sbrk(0) - (char *) sbrk_start) >> 10));
#endif
    storeAppendPrintf(sentry, "\tMaximum Resident Size: %d KB\n",
	rusage_maxrss(&rusage));
    storeAppendPrintf(sentry, "\tPage faults with physical i/o: %d\n",
	rusage_pagefaults(&rusage));

#if HAVE_MSTATS && HAVE_GNUMALLOC_H
    ms = mstats();
    storeAppendPrintf(sentry, "Memory usage for %s via mstats():\n",
	appname);
    storeAppendPrintf(sentry, "\tTotal space in arena:  %6d KB\n",
	(int) (ms.bytes_total >> 10));
    storeAppendPrintf(sentry, "\tTotal free:            %6d KB %d%%\n",
	(int) (ms.bytes_free >> 10), percent(ms.bytes_free, ms.bytes_total));
#elif HAVE_MALLINFO && HAVE_STRUCT_MALLINFO
    mp = mallinfo();
    storeAppendPrintf(sentry, "Memory usage for %s via mallinfo():\n",
	appname);
    storeAppendPrintf(sentry, "\tTotal space in arena:  %6ld KB\n",
	(long int) (mp.arena >> 10));
    storeAppendPrintf(sentry, "\tOrdinary blocks:       %6ld KB %6ld blks\n",
	(long int) (mp.uordblks >> 10), (long int) mp.ordblks);
    storeAppendPrintf(sentry, "\tSmall blocks:          %6ld KB %6ld blks\n",
	(long int) (mp.usmblks >> 10), (long int) mp.smblks);
    storeAppendPrintf(sentry, "\tHolding blocks:        %6ld KB %6ld blks\n",
	(long int) (mp.hblkhd >> 10), (long int) mp.hblks);
    storeAppendPrintf(sentry, "\tFree Small blocks:     %6ld KB\n",
	(long int) (mp.fsmblks >> 10));
    storeAppendPrintf(sentry, "\tFree Ordinary blocks:  %6ld KB\n",
	(long int) (mp.fordblks >> 10));
    t = (mp.uordblks + mp.usmblks + mp.hblkhd) >> 10;
    storeAppendPrintf(sentry, "\tTotal in use:          %6d KB %d%%\n",
	t, percent(t, (mp.arena + mp.hblkhd) >> 10));
    t = (mp.fsmblks + mp.fordblks) >> 10;
    storeAppendPrintf(sentry, "\tTotal free:            %6d KB %d%%\n",
	t, percent(t, (mp.arena + mp.hblkhd) >> 10));
    t = (mp.arena + mp.hblkhd) >> 10;
    storeAppendPrintf(sentry, "\tTotal size:            %6d KB\n",
	t);
#if HAVE_EXT_MALLINFO
    storeAppendPrintf(sentry, "\tmax size of small blocks:\t%d\n", mp.mxfast);
    storeAppendPrintf(sentry, "\tnumber of small blocks in a holding block:\t%d\n",
	mp.nlblks);
    storeAppendPrintf(sentry, "\tsmall block rounding factor:\t%d\n", mp.grain);
    storeAppendPrintf(sentry, "\tspace (including overhead) allocated in ord. blks:\t%d\n"
	,mp.uordbytes);
    storeAppendPrintf(sentry, "\tnumber of ordinary blocks allocated:\t%d\n",
	mp.allocated);
    storeAppendPrintf(sentry, "\tbytes used in maintaining the free tree:\t%d\n",
	mp.treeoverhead);
#endif /* HAVE_EXT_MALLINFO */
#endif /* HAVE_MALLINFO */

    storeAppendPrintf(sentry, "Memory accounted for:\n");
    storeAppendPrintf(sentry, "\tTotal accounted:       %6d KB\n",
	(int) (statMemoryAccounted() >> 10));
    storeAppendPrintf(sentry, "\tmemPoolAlloc calls: %u\n",
	MemPoolStats.alloc_calls);
    storeAppendPrintf(sentry, "\tmemPoolFree calls: %u\n",
	MemPoolStats.free_calls);

    storeAppendPrintf(sentry, "File descriptor usage for %s:\n", appname);
    storeAppendPrintf(sentry, "\tMaximum number of file descriptors:   %4d\n",
	Squid_MaxFD);
    storeAppendPrintf(sentry, "\tLargest file desc currently in use:   %4d\n",
	Biggest_FD);
    storeAppendPrintf(sentry, "\tNumber of file desc currently in use: %4d\n",
	Number_FD);
    storeAppendPrintf(sentry, "\tFiles queued for open:                %4d\n",
	Opening_FD);
    storeAppendPrintf(sentry, "\tAvailable number of file descriptors: %4d\n",
	fdNFree());
    storeAppendPrintf(sentry, "\tReserved number of file descriptors:  %4d\n",
	RESERVED_FD);
    storeAppendPrintf(sentry, "\tStore Disk files open:                %4d\n",
	store_open_disk_fd);
    storeAppendPrintf(sentry, "\tIO loop method:                     %s\n", comm_select_status());

    storeAppendPrintf(sentry, "Internal Data Structures:\n");
    storeAppendPrintf(sentry, "\t%6d StoreEntries\n",
	memPoolInUseCount(pool_storeentry));
    storeAppendPrintf(sentry, "\t%6d StoreEntries with MemObjects\n",
	memPoolInUseCount(pool_memobject));
    storeAppendPrintf(sentry, "\t%6d Hot Object Cache Items\n",
	hot_obj_count);
    storeAppendPrintf(sentry, "\t%6d on-disk objects\n",
	n_disk_objects);

#if XMALLOC_STATISTICS
    xm_deltat = current_dtime - xm_time;
    xm_time = current_dtime;
    storeAppendPrintf(sentry, "\nMemory allocation statistics\n");
    storeAppendPrintf(sentry, "Allocation Size\t Alloc Count\t Alloc Delta\t Allocs/sec \n");
    malloc_statistics(info_get_mallstat, sentry);
#endif
}


static void
info_getex(StoreEntry * sentry, void* data)
{
	if(data)
	{
		DumpInfo(sentry, data);
	}
	else
	{
		info_get(sentry, data);
	}
}


#define XAVG(X) (dt ? (f->X > l->X ? ((double) (f->X - l->X) / dt) : 0.0) : 0.0)
static void
statAvgDump(StoreEntry * sentry, int minutes, int hours)
{
    StatCounters *f;
    StatCounters *l;
    double dt;
    double ct;
    double x;
    assert(N_COUNT_HIST > 1);
    assert(minutes > 0 || hours > 0);
    f = &CountHist[0];
    l = f;
    if (minutes > 0 && hours == 0) {
	/* checking minute readings ... */
	if (minutes > N_COUNT_HIST - 1)
	    minutes = N_COUNT_HIST - 1;
	l = &CountHist[minutes];
    } else if (minutes == 0 && hours > 0) {
	/* checking hour readings ... */
	if (hours > N_COUNT_HOUR_HIST - 1)
	    hours = N_COUNT_HOUR_HIST - 1;
	l = &CountHourHist[hours];
    } else {
	debugs(18, 1, "statAvgDump: Invalid args, minutes=%d, hours=%d",
	    minutes, hours);
	return;
    }
    dt = tvSubDsec(l->timestamp, f->timestamp);
    ct = f->cputime - l->cputime;

    storeAppendPrintf(sentry, "sample_start_time = %d.%d (%s)\n",
	(int) l->timestamp.tv_sec,
	(int) l->timestamp.tv_usec,
	mkrfc1123(l->timestamp.tv_sec));
    storeAppendPrintf(sentry, "sample_end_time = %d.%d (%s)\n",
	(int) f->timestamp.tv_sec,
	(int) f->timestamp.tv_usec,
	mkrfc1123(f->timestamp.tv_sec));

    storeAppendPrintf(sentry, "client_http.requests = %f/sec\n",
	XAVG(client_http.requests));
    storeAppendPrintf(sentry, "client_http.hits = %f/sec\n",
	XAVG(client_http.hits));
    storeAppendPrintf(sentry, "client_http.errors = %f/sec\n",
	XAVG(client_http.errors));
    storeAppendPrintf(sentry, "client_http.kbytes_in = %f/sec\n",
	XAVG(client_http.kbytes_in.kb));
    storeAppendPrintf(sentry, "client_http.kbytes_out = %f/sec\n",
	XAVG(client_http.kbytes_out.kb));

    x = statHistDeltaMedian(&l->client_http.all_svc_time,
	&f->client_http.all_svc_time);
    storeAppendPrintf(sentry, "client_http.all_median_svc_time = %f seconds\n",
	x / 1000.0);
    x = statHistDeltaMedian(&l->client_http.miss_svc_time,
	&f->client_http.miss_svc_time);
    storeAppendPrintf(sentry, "client_http.miss_median_svc_time = %f seconds\n",
	x / 1000.0);
    x = statHistDeltaMedian(&l->client_http.nm_svc_time,
	&f->client_http.nm_svc_time);
    storeAppendPrintf(sentry, "client_http.nm_median_svc_time = %f seconds\n",
	x / 1000.0);
    x = statHistDeltaMedian(&l->client_http.nh_svc_time,
	&f->client_http.nh_svc_time);
    storeAppendPrintf(sentry, "client_http.nh_median_svc_time = %f seconds\n",
	x / 1000.0);
    x = statHistDeltaMedian(&l->client_http.hit_svc_time,
	&f->client_http.hit_svc_time);
    storeAppendPrintf(sentry, "client_http.hit_median_svc_time = %f seconds\n",
	x / 1000.0);

    storeAppendPrintf(sentry, "server.all.requests = %f/sec\n",
	XAVG(server.all.requests));
    storeAppendPrintf(sentry, "server.all.errors = %f/sec\n",
	XAVG(server.all.errors));
    storeAppendPrintf(sentry, "server.all.kbytes_in = %f/sec\n",
	XAVG(server.all.kbytes_in.kb));
    storeAppendPrintf(sentry, "server.all.kbytes_out = %f/sec\n",
	XAVG(server.all.kbytes_out.kb));

    storeAppendPrintf(sentry, "server.http.requests = %f/sec\n",
	XAVG(server.http.requests));
    storeAppendPrintf(sentry, "server.http.errors = %f/sec\n",
	XAVG(server.http.errors));
    storeAppendPrintf(sentry, "server.http.kbytes_in = %f/sec\n",
	XAVG(server.http.kbytes_in.kb));
    storeAppendPrintf(sentry, "server.http.kbytes_out = %f/sec\n",
	XAVG(server.http.kbytes_out.kb));

    storeAppendPrintf(sentry, "server.ftp.requests = %f/sec\n",
	XAVG(server.ftp.requests));
    storeAppendPrintf(sentry, "server.ftp.errors = %f/sec\n",
	XAVG(server.ftp.errors));
    storeAppendPrintf(sentry, "server.ftp.kbytes_in = %f/sec\n",
	XAVG(server.ftp.kbytes_in.kb));
    storeAppendPrintf(sentry, "server.ftp.kbytes_out = %f/sec\n",
	XAVG(server.ftp.kbytes_out.kb));

    storeAppendPrintf(sentry, "server.other.requests = %f/sec\n",
	XAVG(server.other.requests));
    storeAppendPrintf(sentry, "server.other.errors = %f/sec\n",
	XAVG(server.other.errors));
    storeAppendPrintf(sentry, "server.other.kbytes_in = %f/sec\n",
	XAVG(server.other.kbytes_in.kb));
    storeAppendPrintf(sentry, "server.other.kbytes_out = %f/sec\n",
	XAVG(server.other.kbytes_out.kb));

    storeAppendPrintf(sentry, "icp.pkts_sent = %f/sec\n",
	XAVG(icp.pkts_sent));
    storeAppendPrintf(sentry, "icp.pkts_recv = %f/sec\n",
	XAVG(icp.pkts_recv));
    storeAppendPrintf(sentry, "icp.queries_sent = %f/sec\n",
	XAVG(icp.queries_sent));
    storeAppendPrintf(sentry, "icp.replies_sent = %f/sec\n",
	XAVG(icp.replies_sent));
    storeAppendPrintf(sentry, "icp.queries_recv = %f/sec\n",
	XAVG(icp.queries_recv));
    storeAppendPrintf(sentry, "icp.replies_recv = %f/sec\n",
	XAVG(icp.replies_recv));
    storeAppendPrintf(sentry, "icp.replies_queued = %f/sec\n",
	XAVG(icp.replies_queued));
    storeAppendPrintf(sentry, "icp.query_timeouts = %f/sec\n",
	XAVG(icp.query_timeouts));
    storeAppendPrintf(sentry, "icp.kbytes_sent = %f/sec\n",
	XAVG(icp.kbytes_sent.kb));
    storeAppendPrintf(sentry, "icp.kbytes_recv = %f/sec\n",
	XAVG(icp.kbytes_recv.kb));
    storeAppendPrintf(sentry, "icp.q_kbytes_sent = %f/sec\n",
	XAVG(icp.q_kbytes_sent.kb));
    storeAppendPrintf(sentry, "icp.r_kbytes_sent = %f/sec\n",
	XAVG(icp.r_kbytes_sent.kb));
    storeAppendPrintf(sentry, "icp.q_kbytes_recv = %f/sec\n",
	XAVG(icp.q_kbytes_recv.kb));
    storeAppendPrintf(sentry, "icp.r_kbytes_recv = %f/sec\n",
	XAVG(icp.r_kbytes_recv.kb));
    x = statHistDeltaMedian(&l->icp.query_svc_time, &f->icp.query_svc_time);
    storeAppendPrintf(sentry, "icp.query_median_svc_time = %f seconds\n",
	x / 1000000.0);
    x = statHistDeltaMedian(&l->icp.reply_svc_time, &f->icp.reply_svc_time);
    storeAppendPrintf(sentry, "icp.reply_median_svc_time = %f seconds\n",
	x / 1000000.0);
    x = statHistDeltaMedian(&l->dns.svc_time, &f->dns.svc_time);
    storeAppendPrintf(sentry, "dns.median_svc_time = %f seconds\n",
	x / 1000.0);
    storeAppendPrintf(sentry, "unlink.requests = %f/sec\n",
	XAVG(unlink.requests));
    storeAppendPrintf(sentry, "page_faults = %f/sec\n",
	XAVG(page_faults));
#if 0
    storeAppendPrintf(sentry, "select_loops = %f/sec\n",
	XAVG(select_loops));
    storeAppendPrintf(sentry, "select_fds = %f/sec\n",
	XAVG(select_fds));
    storeAppendPrintf(sentry, "average_select_fd_period = %f/fd\n",
	f->select_fds > l->select_fds ?
	(f->select_time - l->select_time) / (f->select_fds - l->select_fds)
	: 0.0);
#endif
    storeAppendPrintf(sentry, "swap.outs = %f/sec\n",
	XAVG(swap.outs));
    storeAppendPrintf(sentry, "swap.ins = %f/sec\n",
	XAVG(swap.ins));
    storeAppendPrintf(sentry, "swap.files_cleaned = %f/sec\n",
	XAVG(swap.files_cleaned));
    storeAppendPrintf(sentry, "aborted_requests = %f/sec\n",
	XAVG(aborted_requests));

#if 0
    if (statCounter.syscalls.polls)
	storeAppendPrintf(sentry, "syscalls.polls = %f/sec\n", XAVG(syscalls.polls));
    if (statCounter.syscalls.selects)
	storeAppendPrintf(sentry, "syscalls.selects = %f/sec\n", XAVG(syscalls.selects));
    storeAppendPrintf(sentry, "syscalls.disk.opens = %f/sec\n", XAVG(syscalls.disk.opens));
    storeAppendPrintf(sentry, "syscalls.disk.closes = %f/sec\n", XAVG(syscalls.disk.closes));
    storeAppendPrintf(sentry, "syscalls.disk.reads = %f/sec\n", XAVG(syscalls.disk.reads));
    storeAppendPrintf(sentry, "syscalls.disk.writes = %f/sec\n", XAVG(syscalls.disk.writes));
    storeAppendPrintf(sentry, "syscalls.disk.seeks = %f/sec\n", XAVG(syscalls.disk.seeks));
    storeAppendPrintf(sentry, "syscalls.disk.unlinks = %f/sec\n", XAVG(syscalls.disk.unlinks));
    storeAppendPrintf(sentry, "syscalls.sock.accepts = %f/sec\n", XAVG(syscalls.sock.accepts));
    storeAppendPrintf(sentry, "syscalls.sock.sockets = %f/sec\n", XAVG(syscalls.sock.sockets));
    storeAppendPrintf(sentry, "syscalls.sock.connects = %f/sec\n", XAVG(syscalls.sock.connects));
    storeAppendPrintf(sentry, "syscalls.sock.binds = %f/sec\n", XAVG(syscalls.sock.binds));
    storeAppendPrintf(sentry, "syscalls.sock.closes = %f/sec\n", XAVG(syscalls.sock.closes));
    storeAppendPrintf(sentry, "syscalls.sock.reads = %f/sec\n", XAVG(syscalls.sock.reads));
    storeAppendPrintf(sentry, "syscalls.sock.writes = %f/sec\n", XAVG(syscalls.sock.writes));
    storeAppendPrintf(sentry, "syscalls.sock.recvfroms = %f/sec\n", XAVG(syscalls.sock.recvfroms));
    storeAppendPrintf(sentry, "syscalls.sock.sendtos = %f/sec\n", XAVG(syscalls.sock.sendtos));
#endif

    storeAppendPrintf(sentry, "cpu_time = %f seconds\n", ct);
    storeAppendPrintf(sentry, "wall_time = %f seconds\n", dt);
    storeAppendPrintf(sentry, "cpu_usage = %f%%\n", dpercent(ct, dt));
}


int addAvgStat(void* A, void* B)
{
	if(!A || !B) return sizeof(IntervalActionData);
	IntervalActionData* stats = (IntervalActionData*)A;
	IntervalActionData* statsB = (IntervalActionData*)B;
	
    if (!timerisset(&stats->sample_start_time) || timercmp(&stats->sample_start_time, &statsB->sample_start_time, >))
        stats->sample_start_time = statsB->sample_start_time;
    if (timercmp(&stats->sample_end_time, &statsB->sample_end_time, <))
        stats->sample_end_time = statsB->sample_end_time;

	stats->client_http_requests  += statsB->client_http_requests;
	stats->client_http_hits  += statsB->client_http_hits;
	stats->client_http_errors  += statsB->client_http_errors;
	stats->client_http_kbytes_in  += statsB->client_http_kbytes_in;
	stats->client_http_kbytes_out  += statsB->client_http_kbytes_out;
	stats->client_http_all_median_svc_time  += statsB->client_http_all_median_svc_time;
	stats->client_http_miss_median_svc_time  += statsB->client_http_miss_median_svc_time;
	stats->client_http_nm_median_svc_time  += statsB->client_http_nm_median_svc_time;
	stats->client_http_nh_median_svc_time  += statsB->client_http_nh_median_svc_time;
	stats->client_http_hit_median_svc_time  += statsB->client_http_hit_median_svc_time;
	stats->server_all_requests  += statsB->server_all_requests;
	stats->server_all_errors  += statsB->server_all_errors;
	stats->server_all_kbytes_in  += statsB->server_all_kbytes_in;
	stats->server_all_kbytes_out  += statsB->server_all_kbytes_out;
	stats->server_http_requests  += statsB->server_http_requests;
	stats->server_http_errors  += statsB->server_http_errors;
	stats->server_http_kbytes_in  += statsB->server_http_kbytes_in;
	stats->server_http_kbytes_out  += statsB->server_http_kbytes_out;
	stats->server_ftp_requests  += statsB->server_ftp_requests;
	stats->server_ftp_errors  += statsB->server_ftp_errors;
	stats->server_ftp_kbytes_in  += statsB->server_ftp_kbytes_in;
	stats->server_ftp_kbytes_out  += statsB->server_ftp_kbytes_out;
	stats->server_other_requests  += statsB->server_other_requests;
	stats->server_other_errors  += statsB->server_other_errors;
	stats->server_other_kbytes_in  += statsB->server_other_kbytes_in;
	stats->server_other_kbytes_out  += statsB->server_other_kbytes_out;
	stats->icp_pkts_sent  += statsB->icp_pkts_sent;
	stats->icp_pkts_recv  += statsB->icp_pkts_recv;
	stats->icp_queries_sent  += statsB->icp_queries_sent;
	stats->icp_replies_sent  += statsB->icp_replies_sent;
	stats->icp_queries_recv  += statsB->icp_queries_recv;
	stats->icp_replies_recv  += statsB->icp_replies_recv;
	stats->icp_replies_queued  += statsB->icp_replies_queued;
	stats->icp_query_timeouts  += statsB->icp_query_timeouts;
	stats->icp_kbytes_sent  += statsB->icp_kbytes_sent;
	stats->icp_kbytes_recv  += statsB->icp_kbytes_recv;
	stats->icp_q_kbytes_sent  += statsB->icp_q_kbytes_sent;
	stats->icp_r_kbytes_sent  += statsB->icp_r_kbytes_sent;
	stats->icp_q_kbytes_recv  += statsB->icp_q_kbytes_recv;
	stats->icp_r_kbytes_recv  += statsB->icp_r_kbytes_recv;
	stats->icp_query_median_svc_time  += statsB->icp_query_median_svc_time;
	stats->icp_reply_median_svc_time  += statsB->icp_reply_median_svc_time;
	stats->dns_median_svc_time  += statsB->dns_median_svc_time;
	stats->unlink_requests  += statsB->unlink_requests;
	stats->page_faults  += statsB->page_faults;
#if 0	
	stats->select_loops  += statsB->select_loops;
	stats->select_fds  += statsB->select_fds;
	stats->average_select_fd_period  += statsB->average_select_fd_period;
#endif
	stats->median_select_fds  += statsB->median_select_fds;
	stats->swap_outs  += statsB->swap_outs;
	stats->swap_ins  += statsB->swap_ins;
	stats->swap_files_cleaned  += statsB->swap_files_cleaned;
	stats->aborted_requests  += statsB->aborted_requests;
#if 0
	stats->syscalls_disk_opens  += statsB->syscalls_disk_opens;
	stats->syscalls_disk_closes  += statsB->syscalls_disk_closes;
	stats->syscalls_disk_reads  += statsB->syscalls_disk_reads;
	stats->syscalls_disk_writes  += statsB->syscalls_disk_writes;
	stats->syscalls_disk_seeks  += statsB->syscalls_disk_seeks;
	stats->syscalls_disk_unlinks  += statsB->syscalls_disk_unlinks;
	stats->syscalls_sock_accepts  += statsB->syscalls_sock_accepts;
	stats->syscalls_sock_sockets  += statsB->syscalls_sock_sockets;
	stats->syscalls_sock_connects  += statsB->syscalls_sock_connects;
	stats->syscalls_sock_binds  += statsB->syscalls_sock_binds;
	stats->syscalls_sock_closes  += statsB->syscalls_sock_closes;
	stats->syscalls_sock_reads  += statsB->syscalls_sock_reads;
	stats->syscalls_sock_writes  += statsB->syscalls_sock_writes;
	stats->syscalls_sock_recvfroms  += statsB->syscalls_sock_recvfroms;
	stats->syscalls_sock_sendtos  += statsB->syscalls_sock_sendtos;
	stats->syscalls_selects  += statsB->syscalls_selects;
	stats->cpu_time  += statsB->cpu_time;
	stats->wall_time  += statsB->wall_time;	
#endif	
	++stats->count;
	return sizeof(IntervalActionData);
}


void* getAvgStat(int minutes, int hours)
{
	IntervalActionData* stats = xcalloc(1, sizeof(IntervalActionData));
	StatCounters *f;
	StatCounters *l;
	double dt;
	double ct;
	assert(N_COUNT_HIST > 1);
	assert(minutes > 0 || hours > 0);
	f = &CountHist[0];
	l = f;

	if (minutes > 0 && hours == 0) {
	 /* checking minute readings ... */

	 if (minutes > N_COUNT_HIST - 1)
		 minutes = N_COUNT_HIST - 1;

	 l = &CountHist[minutes];
	} else if (minutes == 0 && hours > 0) {
	 /* checking hour readings ... */

	 if (hours > N_COUNT_HOUR_HIST - 1)
		 hours = N_COUNT_HOUR_HIST - 1;

	 l = &CountHourHist[hours];
	} else {
	 debugs(18, DBG_IMPORTANT, "statAvgDump: Invalid args, minutes=%d, hours=%d",minutes, hours);
	 return NULL;
	}

	dt = tvSubDsec(l->timestamp, f->timestamp);
	ct = f->cputime - l->cputime;

	stats->sample_start_time = l->timestamp;
	stats->sample_end_time = f->timestamp;

	stats->client_http_requests = XAVG(client_http.requests);
	stats->client_http_hits = XAVG(client_http.hits);
	stats->client_http_errors = XAVG(client_http.errors);
	stats->client_http_kbytes_in = XAVG(client_http.kbytes_in.kb);
	stats->client_http_kbytes_out = XAVG(client_http.kbytes_out.kb);

	stats->client_http_all_median_svc_time = statHistDeltaMedian(&l->client_http.all_svc_time,
										 &f->client_http.all_svc_time) / 1000.0;
	stats->client_http_miss_median_svc_time = statHistDeltaMedian(&l->client_http.miss_svc_time,
		 &f->client_http.miss_svc_time) / 1000.0;
	stats->client_http_nm_median_svc_time = statHistDeltaMedian(&l->client_http.nm_svc_time,
										&f->client_http.nm_svc_time) / 1000.0;
	stats->client_http_nh_median_svc_time = statHistDeltaMedian(&l->client_http.nh_svc_time,
										&f->client_http.nh_svc_time) / 1000.0;
	stats->client_http_hit_median_svc_time = statHistDeltaMedian(&l->client_http.hit_svc_time,
										 &f->client_http.hit_svc_time) / 1000.0;

	stats->server_all_requests = XAVG(server.all.requests);
	stats->server_all_errors = XAVG(server.all.errors);
	stats->server_all_kbytes_in = XAVG(server.all.kbytes_in.kb);
	stats->server_all_kbytes_out = XAVG(server.all.kbytes_out.kb);

	stats->server_http_requests = XAVG(server.http.requests);
	stats->server_http_errors = XAVG(server.http.errors);
	stats->server_http_kbytes_in = XAVG(server.http.kbytes_in.kb);
	stats->server_http_kbytes_out = XAVG(server.http.kbytes_out.kb);

	stats->server_ftp_requests = XAVG(server.ftp.requests);
	stats->server_ftp_errors = XAVG(server.ftp.errors);
	stats->server_ftp_kbytes_in = XAVG(server.ftp.kbytes_in.kb);
	stats->server_ftp_kbytes_out = XAVG(server.ftp.kbytes_out.kb);

	stats->server_other_requests = XAVG(server.other.requests);
	stats->server_other_errors = XAVG(server.other.errors);
	stats->server_other_kbytes_in = XAVG(server.other.kbytes_in.kb);
	stats->server_other_kbytes_out = XAVG(server.other.kbytes_out.kb);

	stats->icp_pkts_sent = XAVG(icp.pkts_sent);
	stats->icp_pkts_recv = XAVG(icp.pkts_recv);
	stats->icp_queries_sent = XAVG(icp.queries_sent);
	stats->icp_replies_sent = XAVG(icp.replies_sent);
	stats->icp_queries_recv = XAVG(icp.queries_recv);
	stats->icp_replies_recv = XAVG(icp.replies_recv);
	stats->icp_replies_queued = XAVG(icp.replies_queued);
	stats->icp_query_timeouts = XAVG(icp.query_timeouts);
	stats->icp_kbytes_sent = XAVG(icp.kbytes_sent.kb);
	stats->icp_kbytes_recv = XAVG(icp.kbytes_recv.kb);
	stats->icp_q_kbytes_sent = XAVG(icp.q_kbytes_sent.kb);
	stats->icp_r_kbytes_sent = XAVG(icp.r_kbytes_sent.kb);
	stats->icp_q_kbytes_recv = XAVG(icp.q_kbytes_recv.kb);
	stats->icp_r_kbytes_recv = XAVG(icp.r_kbytes_recv.kb);

	stats->icp_query_median_svc_time = statHistDeltaMedian(&l->icp.query_svc_time,
								   &f->icp.query_svc_time) / 1000000.0;
	stats->icp_reply_median_svc_time = statHistDeltaMedian(&l->icp.reply_svc_time,
								   &f->icp.reply_svc_time) / 1000000.0;
	stats->dns_median_svc_time = statHistDeltaMedian(&l->dns.svc_time,
							 &f->dns.svc_time) / 1000.0;

	stats->unlink_requests = XAVG(unlink.requests);
	stats->page_faults = XAVG(page_faults);

#if 0	
	stats->select_loops = XAVG(select_loops);
	stats->select_fds = XAVG(select_fds);	
	stats->average_select_fd_period = f->select_fds > l->select_fds ?
								  (f->select_time - l->select_time) / (f->select_fds - l->select_fds) : 0.0;
#endif	
	stats->median_select_fds = statHistDeltaMedian(&l->select_fds_hist, &f->select_fds_hist);
	stats->swap_outs = XAVG(swap.outs);
	stats->swap_ins = XAVG(swap.ins);
	stats->swap_files_cleaned = XAVG(swap.files_cleaned);
	stats->aborted_requests = XAVG(aborted_requests);

#if 0	
	stats->syscalls_disk_opens = XAVG(syscalls.disk.opens);
	stats->syscalls_disk_closes = XAVG(syscalls.disk.closes);
	stats->syscalls_disk_reads = XAVG(syscalls.disk.reads);
	stats->syscalls_disk_writes = XAVG(syscalls.disk.writes);
	stats->syscalls_disk_seeks = XAVG(syscalls.disk.seeks);
	stats->syscalls_disk_unlinks = XAVG(syscalls.disk.unlinks);
	stats->syscalls_sock_accepts = XAVG(syscalls.sock.accepts);
	stats->syscalls_sock_sockets = XAVG(syscalls.sock.sockets);
	stats->syscalls_sock_connects = XAVG(syscalls.sock.connects);
	stats->syscalls_sock_binds = XAVG(syscalls.sock.binds);
	stats->syscalls_sock_closes = XAVG(syscalls.sock.closes);
	stats->syscalls_sock_reads = XAVG(syscalls.sock.reads);
	stats->syscalls_sock_writes = XAVG(syscalls.sock.writes);
	stats->syscalls_sock_recvfroms = XAVG(syscalls.sock.recvfroms);
	stats->syscalls_sock_sendtos = XAVG(syscalls.sock.sendtos);
	stats->syscalls_selects = XAVG(syscalls.selects);*/
#endif

	stats->cpu_time = ct;
	stats->wall_time = dt;

	stats->count = 1;
	
	return (void*)stats;
}

void
DumpAvgStat(StoreEntry* sentry, void* data)
{
	IntervalActionData* stats = (IntervalActionData*)data;
		
    storeAppendPrintf(sentry, "sample_start_time = %d.%d (%s)\n",
                      (int)stats->sample_start_time.tv_sec,
                      (int)stats->sample_start_time.tv_usec,
                      mkrfc1123(stats->sample_start_time.tv_sec));
    storeAppendPrintf(sentry, "sample_end_time = %d.%d (%s)\n",
                      (int)stats->sample_end_time.tv_sec,
                      (int)stats->sample_end_time.tv_usec,
                      mkrfc1123(stats->sample_end_time.tv_sec));

    storeAppendPrintf(sentry, "client_http.requests = %f/sec\n",
                      stats->client_http_requests);
    storeAppendPrintf(sentry, "client_http.hits = %f/sec\n",
                      stats->client_http_hits);
    storeAppendPrintf(sentry, "client_http.errors = %f/sec\n",
                      stats->client_http_errors);
    storeAppendPrintf(sentry, "client_http.kbytes_in = %f/sec\n",
                      stats->client_http_kbytes_in);
    storeAppendPrintf(sentry, "client_http.kbytes_out = %f/sec\n",
                      stats->client_http_kbytes_out);

    double fct = stats->count > 1 ? stats->count : 1.0;
    storeAppendPrintf(sentry, "client_http.all_median_svc_time = %f seconds\n",
                      stats->client_http_all_median_svc_time / fct);
    storeAppendPrintf(sentry, "client_http.miss_median_svc_time = %f seconds\n",
                      stats->client_http_miss_median_svc_time / fct);
    storeAppendPrintf(sentry, "client_http.nm_median_svc_time = %f seconds\n",
                      stats->client_http_nm_median_svc_time / fct);
    storeAppendPrintf(sentry, "client_http.nh_median_svc_time = %f seconds\n",
                      stats->client_http_nh_median_svc_time / fct);
    storeAppendPrintf(sentry, "client_http.hit_median_svc_time = %f seconds\n",
                      stats->client_http_hit_median_svc_time / fct);

    storeAppendPrintf(sentry, "server.all.requests = %f/sec\n",
                      stats->server_all_requests);
    storeAppendPrintf(sentry, "server.all.errors = %f/sec\n",
                      stats->server_all_errors);
    storeAppendPrintf(sentry, "server.all.kbytes_in = %f/sec\n",
                      stats->server_all_kbytes_in);
    storeAppendPrintf(sentry, "server.all.kbytes_out = %f/sec\n",
                      stats->server_all_kbytes_out);

    storeAppendPrintf(sentry, "server.http.requests = %f/sec\n",
                      stats->server_http_requests);
    storeAppendPrintf(sentry, "server.http.errors = %f/sec\n",
                      stats->server_http_errors);
    storeAppendPrintf(sentry, "server.http.kbytes_in = %f/sec\n",
                      stats->server_http_kbytes_in);
    storeAppendPrintf(sentry, "server.http.kbytes_out = %f/sec\n",
                      stats->server_http_kbytes_out);

    storeAppendPrintf(sentry, "server.ftp.requests = %f/sec\n",
                      stats->server_ftp_requests);
    storeAppendPrintf(sentry, "server.ftp.errors = %f/sec\n",
                      stats->server_ftp_errors);
    storeAppendPrintf(sentry, "server.ftp.kbytes_in = %f/sec\n",
                      stats->server_ftp_kbytes_in);
    storeAppendPrintf(sentry, "server.ftp.kbytes_out = %f/sec\n",
                      stats->server_ftp_kbytes_out);

    storeAppendPrintf(sentry, "server.other.requests = %f/sec\n",
                      stats->server_other_requests);
    storeAppendPrintf(sentry, "server.other.errors = %f/sec\n",
                      stats->server_other_errors);
    storeAppendPrintf(sentry, "server.other.kbytes_in = %f/sec\n",
                      stats->server_other_kbytes_in);
    storeAppendPrintf(sentry, "server.other.kbytes_out = %f/sec\n",
                      stats->server_other_kbytes_out);

    storeAppendPrintf(sentry, "icp.pkts_sent = %f/sec\n",
                      stats->icp_pkts_sent);
    storeAppendPrintf(sentry, "icp.pkts_recv = %f/sec\n",
                      stats->icp_pkts_recv);
    storeAppendPrintf(sentry, "icp.queries_sent = %f/sec\n",
                      stats->icp_queries_sent);
    storeAppendPrintf(sentry, "icp.replies_sent = %f/sec\n",
                      stats->icp_replies_sent);
    storeAppendPrintf(sentry, "icp.queries_recv = %f/sec\n",
                      stats->icp_queries_recv);
    storeAppendPrintf(sentry, "icp.replies_recv = %f/sec\n",
                      stats->icp_replies_recv);
    storeAppendPrintf(sentry, "icp.replies_queued = %f/sec\n",
                      stats->icp_replies_queued);
    storeAppendPrintf(sentry, "icp.query_timeouts = %f/sec\n",
                      stats->icp_query_timeouts);
    storeAppendPrintf(sentry, "icp.kbytes_sent = %f/sec\n",
                      stats->icp_kbytes_sent);
    storeAppendPrintf(sentry, "icp.kbytes_recv = %f/sec\n",
                      stats->icp_kbytes_recv);
    storeAppendPrintf(sentry, "icp.q_kbytes_sent = %f/sec\n",
                      stats->icp_q_kbytes_sent);
    storeAppendPrintf(sentry, "icp.r_kbytes_sent = %f/sec\n",
                      stats->icp_r_kbytes_sent);
    storeAppendPrintf(sentry, "icp.q_kbytes_recv = %f/sec\n",
                      stats->icp_q_kbytes_recv);
    storeAppendPrintf(sentry, "icp.r_kbytes_recv = %f/sec\n",
                      stats->icp_r_kbytes_recv);
    storeAppendPrintf(sentry, "icp.query_median_svc_time = %f seconds\n",
                      stats->icp_query_median_svc_time / fct);
    storeAppendPrintf(sentry, "icp.reply_median_svc_time = %f seconds\n",
                      stats->icp_reply_median_svc_time / fct);
    storeAppendPrintf(sentry, "dns.median_svc_time = %f seconds\n",
                      stats->dns_median_svc_time / fct);
    storeAppendPrintf(sentry, "unlink.requests = %f/sec\n",
                      stats->unlink_requests);
    storeAppendPrintf(sentry, "page_faults = %f/sec\n",
                      stats->page_faults);
#if 0
    storeAppendPrintf(sentry, "select_loops = %f/sec\n",
                      stats->select_loops);
    storeAppendPrintf(sentry, "select_fds = %f/sec\n",
                      stats->select_fds);
    storeAppendPrintf(sentry, "average_select_fd_period = %f/fd\n",
                      stats->average_select_fd_period / fct);
#endif	
    storeAppendPrintf(sentry, "median_select_fds = %f\n",
                      stats->median_select_fds / fct);
    storeAppendPrintf(sentry, "swap.outs = %f/sec\n",
                      stats->swap_outs);
    storeAppendPrintf(sentry, "swap.ins = %f/sec\n",
                      stats->swap_ins);
    storeAppendPrintf(sentry, "swap.files_cleaned = %f/sec\n",
                      stats->swap_files_cleaned);
    storeAppendPrintf(sentry, "aborted_requests = %f/sec\n",
                      stats->aborted_requests);

#if USE_POLL
    storeAppendPrintf(sentry, "syscalls.polls = %f/sec\n", stats->syscalls_selects);
#elif defined(USE_SELECT) || defined(USE_SELECT_WIN32)
    storeAppendPrintf(sentry, "syscalls.selects = %f/sec\n", stats->syscalls_selects);
#endif

#if 0	
    storeAppendPrintf(sentry, "syscalls.disk.opens = %f/sec\n", stats->syscalls_disk_opens);
    storeAppendPrintf(sentry, "syscalls.disk.closes = %f/sec\n", stats->syscalls_disk_closes);
    storeAppendPrintf(sentry, "syscalls.disk.reads = %f/sec\n", stats->syscalls_disk_reads);
    storeAppendPrintf(sentry, "syscalls.disk.writes = %f/sec\n", stats->syscalls_disk_writes);
    storeAppendPrintf(sentry, "syscalls.disk.seeks = %f/sec\n", stats->syscalls_disk_seeks);
    storeAppendPrintf(sentry, "syscalls.disk.unlinks = %f/sec\n", stats->syscalls_disk_unlinks);
    storeAppendPrintf(sentry, "syscalls.sock.accepts = %f/sec\n", stats->syscalls_sock_accepts);
    storeAppendPrintf(sentry, "syscalls.sock.sockets = %f/sec\n", stats->syscalls_sock_sockets);
    storeAppendPrintf(sentry, "syscalls.sock.connects = %f/sec\n", stats->syscalls_sock_connects);
    storeAppendPrintf(sentry, "syscalls.sock.binds = %f/sec\n", stats->syscalls_sock_binds);
    storeAppendPrintf(sentry, "syscalls.sock.closes = %f/sec\n", stats->syscalls_sock_closes);
    storeAppendPrintf(sentry, "syscalls.sock.reads = %f/sec\n", stats->syscalls_sock_reads);
    storeAppendPrintf(sentry, "syscalls.sock.writes = %f/sec\n", stats->syscalls_sock_writes);
    storeAppendPrintf(sentry, "syscalls.sock.recvfroms = %f/sec\n", stats->syscalls_sock_recvfroms);
    storeAppendPrintf(sentry, "syscalls.sock.sendtos = %f/sec\n", stats->syscalls_sock_sendtos);
#endif

    storeAppendPrintf(sentry, "cpu_time = %f seconds\n", stats->cpu_time);
    storeAppendPrintf(sentry, "wall_time = %f seconds\n", stats->wall_time);
    storeAppendPrintf(sentry, "cpu_usage = %f%%\n", dpercent(stats->cpu_time, stats->wall_time));
}

void
statInit(void)
{
    int i;
    debugs(18, 5, "statInit: Initializing...");
    CBDATA_INIT_TYPE(StatObjectsState);
    for (i = 0; i < N_COUNT_HIST; i++)
	statCountersInit(&CountHist[i]);
    for (i = 0; i < N_COUNT_HOUR_HIST; i++)
	statCountersInit(&CountHourHist[i]);
    statCountersInit(&statCounter);
    eventAdd("statAvgTick", statAvgTick, NULL, (double) COUNT_INTERVAL, 1);
    cachemgrRegister("info",
	"General Runtime Information",
	info_getex, add_info, get_info, 0, 1, 1);
    cachemgrRegister("filedescriptors",
	"Process Filedescriptor Allocation",
	statFiledescriptors, NULL, NULL, 0, 1, 0);
    cachemgrRegister("objects",
	"All Cache Objects",
	stat_objects_get, NULL, NULL, 0, 0, 0);
    cachemgrRegister("vm_objects",
	"In-Memory and In-Transit Objects",
	stat_vmobjects_get, NULL, NULL, 0, 0, 0);
    cachemgrRegister("openfd_objects",
	"Objects with Swapout files open",
	statOpenfdObj, NULL, NULL, 0, 0, 0);
    cachemgrRegister("pending_objects",
	"Objects being retreived from the network",
	statPendingObj, NULL, NULL, 0, 0, 0);
    cachemgrRegister("client_objects",
	"Objects being sent to clients",
	statClientsObj, NULL, NULL, 0, 0, 0);
    cachemgrRegister("io",
	"Server-side network read() size histograms",
	stat_io_getex, AddIoStats, GetIoStats, 0, 1, 1);
    cachemgrRegister("counters",
	"Traffic and Resource Counters",
	statCountersDumpEx, AddCountersStats, GetCountersStats, 0, 1, 1);
    cachemgrRegister("peer_select",
	"Peer Selection Algorithms",
	statPeerSelect, NULL, NULL, 0, 1, 0);
    cachemgrRegister("digest_stats",
	"Cache Digest and ICP blob",
	statDigestBlob, NULL, NULL, 0, 1, 0);
    cachemgrRegister("5min",
	"5 Minute Average of Counters",
	statAvg5minEx, addAvgStat, getAvg5min, 0, 1, 1);
    cachemgrRegister("60min",
	"60 Minute Average of Counters",
	statAvg60minEx, addAvgStat, getAvg60min, 0, 1, 1);
    cachemgrRegister("utilization",
	"Cache Utilization",
	statUtilization, NULL, NULL, 0, 1, 0);
#if STAT_GRAPHS
    cachemgrRegister("graph_variables",
	"Display cache metrics graphically",
	statGraphDump, NULL, NULL, 0, 1, 0);
#endif
    cachemgrRegister("histograms",
	"Full Histogram Counts",
	statCountersHistograms, NULL, NULL, 0, 1, 0);
    ClientActiveRequests.head = NULL;
    ClientActiveRequests.tail = NULL;
    cachemgrRegister("active_requests",
	"Client-side Active Requests",
	statClientRequests, NULL, NULL, 0, 1, 0);
    cachemgrRegister("iapp_stats", "libiapp statistics", statIappStats, NULL, NULL, 0, 1, 0);
    cachemgrRegister("curcounters", "current high level counters", statCurrentStuff, NULL, NULL, 0, 1, 0);
    cachemgrRegister("squidaio_counts", "Async IO Function Counters", aioStats, NULL, NULL, 0, 1, 0);
}

static void
statAvgTick(void *notused)
{
    StatCounters *t = &CountHist[0];
    StatCounters *p = &CountHist[1];
    StatCounters *c = &statCounter;
    struct rusage rusage;
    eventAdd("statAvgTick", statAvgTick, NULL, (double) COUNT_INTERVAL, 1);
    squid_getrusage(&rusage);
    c->page_faults = rusage_pagefaults(&rusage);
    c->cputime = rusage_cputime(&rusage);
    c->timestamp = current_time;
    /* even if NCountHist is small, we already Init()ed the tail */
    statCountersClean(CountHist + N_COUNT_HIST - 1);
    xmemmove(p, t, (N_COUNT_HIST - 1) * sizeof(StatCounters));
    statCountersCopy(t, c);
    NCountHist++;

    if ((NCountHist % COUNT_INTERVAL) == 0) {
	/* we have an hours worth of readings.  store previous hour */
	StatCounters *t = &CountHourHist[0];
	StatCounters *p = &CountHourHist[1];
	StatCounters *c = &CountHist[N_COUNT_HIST - 1];
	statCountersClean(CountHourHist + N_COUNT_HOUR_HIST - 1);
	xmemmove(p, t, (N_COUNT_HOUR_HIST - 1) * sizeof(StatCounters));
	statCountersCopy(t, c);
	NCountHourHist++;
    }
    if (Config.warnings.high_rptm > 0) {
	int i = (int) statMedianSvc(20, MEDIAN_HTTP);
	if (Config.warnings.high_rptm < i)
	    debugs(18, 0, "WARNING: Median response time is %d milliseconds", i);
    }
    if (Config.warnings.high_pf) {
	int i = (CountHist[0].page_faults - CountHist[1].page_faults);
	double dt = tvSubDsec(CountHist[0].timestamp, CountHist[1].timestamp);
	if (i > 0 && dt > 0.0) {
	    i /= (int) dt;
	    if (Config.warnings.high_pf < i)
		debugs(18, 0, "WARNING: Page faults occuring at %d/sec", i);
	}
    }
    if (Config.warnings.high_memory) {
	int i = 0;
#if HAVE_MSTATS && HAVE_GNUMALLOC_H
	struct mstats ms = mstats();
	i = ms.bytes_total;
#elif HAVE_MALLINFO && HAVE_STRUCT_MALLINFO
	struct mallinfo mp = mallinfo();
	i = mp.arena;
#elif HAVE_SBRK
	i = (size_t) ((char *) sbrk(0) - (char *) sbrk_start);
#endif
	if (Config.warnings.high_memory < i)
	    debugs(18, 0, "WARNING: Memory usage at %d MB", i >> 20);
    }
}

static void
statCountersInit(StatCounters * C)
{
    assert(C);
    memset(C, 0, sizeof(*C));
    C->timestamp = current_time;
    statCountersInitSpecial(C);
}

/* add special cases here as they arrive */
static void
statCountersInitSpecial(StatCounters * C)
{
    /*
     * HTTP svc_time hist is kept in milli-seconds; max of 3 hours.
     */
    statHistLogInit(&C->client_http.all_svc_time, 300, 0.0, 3600000.0 * 3.0);
    statHistLogInit(&C->client_http.miss_svc_time, 300, 0.0, 3600000.0 * 3.0);
    statHistLogInit(&C->client_http.nm_svc_time, 300, 0.0, 3600000.0 * 3.0);
    statHistLogInit(&C->client_http.nh_svc_time, 300, 0.0, 3600000.0 * 3.0);
    statHistLogInit(&C->client_http.hit_svc_time, 300, 0.0, 3600000.0 * 3.0);
    /*
     * ICP svc_time hist is kept in micro-seconds; max of 1 minute.
     */
    statHistLogInit(&C->icp.query_svc_time, 300, 0.0, 1000000.0 * 60.0);
    statHistLogInit(&C->icp.reply_svc_time, 300, 0.0, 1000000.0 * 60.0);
    /*
     * DNS svc_time hist is kept in milli-seconds; max of 10 minutes.
     */
    statHistLogInit(&C->dns.svc_time, 300, 0.0, 60000.0 * 10.0);
    /*
     * Cache Digest Stuff
     */
    statHistEnumInit(&C->cd.on_xition_count, CacheDigestHashFuncCount);
    statHistEnumInit(&C->comm_icp_incoming, INCOMING_ICP_MAX);
    statHistEnumInit(&C->comm_dns_incoming, INCOMING_DNS_MAX);
    statHistEnumInit(&C->comm_http_incoming, INCOMING_HTTP_MAX);
}

/* add special cases here as they arrive */
static void
statCountersClean(StatCounters * C)
{
    assert(C);
    statHistClean(&C->client_http.all_svc_time);
    statHistClean(&C->client_http.miss_svc_time);
    statHistClean(&C->client_http.nm_svc_time);
    statHistClean(&C->client_http.nh_svc_time);
    statHistClean(&C->client_http.hit_svc_time);
    statHistClean(&C->icp.query_svc_time);
    statHistClean(&C->icp.reply_svc_time);
    statHistClean(&C->dns.svc_time);
    statHistClean(&C->cd.on_xition_count);
    statHistClean(&C->comm_icp_incoming);
    statHistClean(&C->comm_dns_incoming);
    statHistClean(&C->comm_http_incoming);
}

/* add special cases here as they arrive */
static void
statCountersCopy(StatCounters * dest, const StatCounters * orig)
{
    assert(dest && orig);
    /* this should take care of all the fields, but "special" ones */
    xmemcpy(dest, orig, sizeof(*dest));
    /* prepare space where to copy special entries */
    statCountersInitSpecial(dest);
    /* now handle special cases */
    /* note: we assert that histogram capacities do not change */
    statHistCopy(&dest->client_http.all_svc_time, &orig->client_http.all_svc_time);
    statHistCopy(&dest->client_http.miss_svc_time, &orig->client_http.miss_svc_time);
    statHistCopy(&dest->client_http.nm_svc_time, &orig->client_http.nm_svc_time);
    statHistCopy(&dest->client_http.nh_svc_time, &orig->client_http.nh_svc_time);
    statHistCopy(&dest->client_http.hit_svc_time, &orig->client_http.hit_svc_time);
    statHistCopy(&dest->icp.query_svc_time, &orig->icp.query_svc_time);
    statHistCopy(&dest->icp.reply_svc_time, &orig->icp.reply_svc_time);
    statHistCopy(&dest->dns.svc_time, &orig->dns.svc_time);
    statHistCopy(&dest->cd.on_xition_count, &orig->cd.on_xition_count);
    statHistCopy(&dest->comm_icp_incoming, &orig->comm_icp_incoming);
    statHistCopy(&dest->comm_http_incoming, &orig->comm_http_incoming);
}

static void
statCountersHistograms(StoreEntry * sentry, void* data)
{
    StatCounters *f = &statCounter;
    storeAppendPrintf(sentry, "client_http.all_svc_time histogram:\n");
    statHistDump(&f->client_http.all_svc_time, sentry, NULL);
    storeAppendPrintf(sentry, "client_http.miss_svc_time histogram:\n");
    statHistDump(&f->client_http.miss_svc_time, sentry, NULL);
    storeAppendPrintf(sentry, "client_http.nm_svc_time histogram:\n");
    statHistDump(&f->client_http.nm_svc_time, sentry, NULL);
    storeAppendPrintf(sentry, "client_http.nh_svc_time histogram:\n");
    statHistDump(&f->client_http.nh_svc_time, sentry, NULL);
    storeAppendPrintf(sentry, "client_http.hit_svc_time histogram:\n");
    statHistDump(&f->client_http.hit_svc_time, sentry, NULL);
    storeAppendPrintf(sentry, "icp.query_svc_time histogram:\n");
    statHistDump(&f->icp.query_svc_time, sentry, NULL);
    storeAppendPrintf(sentry, "icp.reply_svc_time histogram:\n");
    statHistDump(&f->icp.reply_svc_time, sentry, NULL);
    storeAppendPrintf(sentry, "dns.svc_time histogram:\n");
    statHistDump(&f->dns.svc_time, sentry, NULL);
}

int AddCountersStats(void* A, void* B)
{
	if(!A || !B) return sizeof(CountersActionData);
	CountersActionData* stats = (CountersActionData*)A;
	
	CountersActionData* statsB = (CountersActionData*)B;
	
	if (timercmp(&stats->sample_time, &statsB->sample_time, <))
		 stats->sample_time = statsB->sample_time;
	
	stats->client_http_requests += statsB->client_http_requests;
	stats->client_http_hits += statsB->client_http_hits;
	stats->client_http_errors = statsB->client_http_errors;
	stats->client_http_kbytes_in = statsB->client_http_kbytes_in;
	stats->client_http_kbytes_out = statsB->client_http_kbytes_out;
	stats->client_http_hit_kbytes_out = statsB->client_http_hit_kbytes_out;

	stats->server_all_requests	+=	statsB->server_all_requests;
	stats->server_all_errors  +=	statsB->server_all_errors;
	stats->server_all_kbytes_in  += 	statsB->server_all_kbytes_in;
	stats->server_all_kbytes_out  +=	statsB->server_all_kbytes_out;
	stats->server_http_requests  += 	statsB->server_http_requests;
	stats->server_http_errors  +=	statsB->server_http_errors;
	stats->server_http_kbytes_in  +=	statsB->server_http_kbytes_in;
	stats->server_http_kbytes_out  +=	statsB->server_http_kbytes_out;
	stats->server_ftp_requests	+=	statsB->server_ftp_requests;
	stats->server_ftp_errors  +=	statsB->server_ftp_errors;
	stats->server_ftp_kbytes_in  += 	statsB->server_ftp_kbytes_in;
	stats->server_ftp_kbytes_out  +=	statsB->server_ftp_kbytes_out;
	stats->server_other_requests  +=	statsB->server_other_requests;
	stats->server_other_errors	+=	statsB->server_other_errors;
	stats->server_other_kbytes_in  +=	statsB->server_other_kbytes_in;
	stats->server_other_kbytes_out	+=	statsB->server_other_kbytes_out;
	stats->icp_pkts_sent  +=	statsB->icp_pkts_sent;
	stats->icp_pkts_recv  +=	statsB->icp_pkts_recv;
	stats->icp_queries_sent  += 	statsB->icp_queries_sent;
	stats->icp_replies_sent  += 	statsB->icp_replies_sent;
	stats->icp_queries_recv  += 	statsB->icp_queries_recv;
	stats->icp_replies_recv  += 	statsB->icp_replies_recv;
	stats->icp_query_timeouts  +=	statsB->icp_query_timeouts;
	stats->icp_replies_queued  +=	statsB->icp_replies_queued;
	stats->icp_kbytes_sent	+=	statsB->icp_kbytes_sent;
	stats->icp_kbytes_recv	+=	statsB->icp_kbytes_recv;
	stats->icp_q_kbytes_sent  +=	statsB->icp_q_kbytes_sent;
	stats->icp_r_kbytes_sent  +=	statsB->icp_r_kbytes_sent;
	stats->icp_q_kbytes_recv  +=	statsB->icp_q_kbytes_recv;
	stats->icp_r_kbytes_recv  +=	statsB->icp_r_kbytes_recv;
#if USE_CACHE_DIGESTS
	stats->icp_times_used  +=	statsB->icp_times_used;
	stats->cd_times_used  +=	statsB->cd_times_used;
	stats->cd_msgs_sent  += 	statsB->cd_msgs_sent;
	stats->cd_msgs_recv  += 	statsB->cd_msgs_recv;
	stats->cd_memory  +=	statsB->cd_memory;
	stats->cd_local_memory	+=	statsB->cd_local_memory;
	stats->cd_kbytes_sent  +=	statsB->cd_kbytes_sent;
	stats->cd_kbytes_recv  +=	statsB->cd_kbytes_recv;
#endif
	stats->unlink_requests	+=	statsB->unlink_requests;
	stats->page_faults	+=	statsB->page_faults;

	stats->cpu_time  += 	statsB->cpu_time;
	stats->wall_time  +=	statsB->wall_time;
	stats->swap_outs  +=	statsB->swap_outs; 
	stats->swap_ins  += 	statsB->swap_ins;
	stats->swap_files_cleaned  +=	statsB->swap_files_cleaned;
	stats->aborted_requests  += 	statsB->aborted_requests;

	return sizeof(CountersActionData);
}

void* GetCountersStats()
{
	CountersActionData* stats = xcalloc(1, sizeof(CountersActionData));
	
    StatCounters *f = &statCounter;

    struct rusage rusage;
    squid_getrusage(&rusage);
    f->page_faults = rusage_pagefaults(&rusage);
    f->cputime = rusage_cputime(&rusage);

    stats->sample_time = f->timestamp;
    stats->client_http_requests = f->client_http.requests;
    stats->client_http_hits = f->client_http.hits;
    stats->client_http_errors = f->client_http.errors;
    stats->client_http_kbytes_in = f->client_http.kbytes_in.kb;
    stats->client_http_kbytes_out = f->client_http.kbytes_out.kb;
    stats->client_http_hit_kbytes_out = f->client_http.hit_kbytes_out.kb;

    stats->server_all_requests = f->server.all.requests;
    stats->server_all_errors = f->server.all.errors;
    stats->server_all_kbytes_in = f->server.all.kbytes_in.kb;
    stats->server_all_kbytes_out = f->server.all.kbytes_out.kb;

    stats->server_http_requests = f->server.http.requests;
    stats->server_http_errors = f->server.http.errors;
    stats->server_http_kbytes_in = f->server.http.kbytes_in.kb;
    stats->server_http_kbytes_out = f->server.http.kbytes_out.kb;

    stats->server_ftp_requests = f->server.ftp.requests;
    stats->server_ftp_errors = f->server.ftp.errors;
    stats->server_ftp_kbytes_in = f->server.ftp.kbytes_in.kb;
    stats->server_ftp_kbytes_out = f->server.ftp.kbytes_out.kb;

    stats->server_other_requests = f->server.other.requests;
    stats->server_other_errors = f->server.other.errors;
    stats->server_other_kbytes_in = f->server.other.kbytes_in.kb;
    stats->server_other_kbytes_out = f->server.other.kbytes_out.kb;

    stats->icp_pkts_sent = f->icp.pkts_sent;
    stats->icp_pkts_recv = f->icp.pkts_recv;
    stats->icp_queries_sent = f->icp.queries_sent;
    stats->icp_replies_sent = f->icp.replies_sent;
    stats->icp_queries_recv = f->icp.queries_recv;
    stats->icp_replies_recv = f->icp.replies_recv;
    stats->icp_query_timeouts = f->icp.query_timeouts;
    stats->icp_replies_queued = f->icp.replies_queued;
    stats->icp_kbytes_sent = f->icp.kbytes_sent.kb;
    stats->icp_kbytes_recv = f->icp.kbytes_recv.kb;
    stats->icp_q_kbytes_sent = f->icp.q_kbytes_sent.kb;
    stats->icp_r_kbytes_sent = f->icp.r_kbytes_sent.kb;
    stats->icp_q_kbytes_recv = f->icp.q_kbytes_recv.kb;
    stats->icp_r_kbytes_recv = f->icp.r_kbytes_recv.kb;

#if USE_CACHE_DIGESTS

    stats->icp_times_used = f->icp.times_used;
    stats->cd_times_used = f->cd.times_used;
    stats->cd_msgs_sent = f->cd.msgs_sent;
    stats->cd_msgs_recv = f->cd.msgs_recv;
    stats->cd_memory = f->cd.memory.kb;
    stats->cd_local_memory = store_digest ? store_digest->mask_size / 1024 : 0;
    stats->cd_kbytes_sent = f->cd.kbytes_sent.kb;
    stats->cd_kbytes_recv = f->cd.kbytes_recv.kb;
#endif

    stats->unlink_requests = f->unlink.requests;
    stats->page_faults = f->page_faults;
    stats->cpu_time = f->cputime;
    stats->wall_time = tvSubDsec(f->timestamp, current_time);
    stats->swap_outs = f->swap.outs;
    stats->swap_ins = f->swap.ins;
    stats->swap_files_cleaned = f->swap.files_cleaned;
    stats->aborted_requests = f->aborted_requests;
	return (void*)stats;
}


void
DumpCountersStats(StoreEntry* sentry, void* data)
{
	CountersActionData* stats = (CountersActionData*)data;
	
    storeAppendPrintf(sentry, "sample_time = %d.%d (%s)\n",
                      (int) stats->sample_time.tv_sec,
                      (int) stats->sample_time.tv_usec,
                      mkrfc1123(stats->sample_time.tv_sec));
    storeAppendPrintf(sentry, "client_http.requests = %.0f\n",
                      stats->client_http_requests);
    storeAppendPrintf(sentry, "client_http.hits = %.0f\n",
                      stats->client_http_hits);
    storeAppendPrintf(sentry, "client_http.errors = %.0f\n",
                      stats->client_http_errors);
    storeAppendPrintf(sentry, "client_http.kbytes_in = %.0f\n",
                      stats->client_http_kbytes_in);
    storeAppendPrintf(sentry, "client_http.kbytes_out = %.0f\n",
                      stats->client_http_kbytes_out);
    storeAppendPrintf(sentry, "client_http.hit_kbytes_out = %.0f\n",
                      stats->client_http_hit_kbytes_out);

    storeAppendPrintf(sentry, "server.all.requests = %.0f\n",
                      stats->server_all_requests);
    storeAppendPrintf(sentry, "server.all.errors = %.0f\n",
                      stats->server_all_errors);
    storeAppendPrintf(sentry, "server.all.kbytes_in = %.0f\n",
                      stats->server_all_kbytes_in);
    storeAppendPrintf(sentry, "server.all.kbytes_out = %.0f\n",
                      stats->server_all_kbytes_out);

    storeAppendPrintf(sentry, "server.http.requests = %.0f\n",
                      stats->server_http_requests);
    storeAppendPrintf(sentry, "server.http.errors = %.0f\n",
                      stats->server_http_errors);
    storeAppendPrintf(sentry, "server.http.kbytes_in = %.0f\n",
                      stats->server_http_kbytes_in);
    storeAppendPrintf(sentry, "server.http.kbytes_out = %.0f\n",
                      stats->server_http_kbytes_out);

    storeAppendPrintf(sentry, "server.ftp.requests = %.0f\n",
                      stats->server_ftp_requests);
    storeAppendPrintf(sentry, "server.ftp.errors = %.0f\n",
                      stats->server_ftp_errors);
    storeAppendPrintf(sentry, "server.ftp.kbytes_in = %.0f\n",
                      stats->server_ftp_kbytes_in);
    storeAppendPrintf(sentry, "server.ftp.kbytes_out = %.0f\n",
                      stats->server_ftp_kbytes_out);

    storeAppendPrintf(sentry, "server.other.requests = %.0f\n",
                      stats->server_other_requests);
    storeAppendPrintf(sentry, "server.other.errors = %.0f\n",
                      stats->server_other_errors);
    storeAppendPrintf(sentry, "server.other.kbytes_in = %.0f\n",
                      stats->server_other_kbytes_in);
    storeAppendPrintf(sentry, "server.other.kbytes_out = %.0f\n",
                      stats->server_other_kbytes_out);

    storeAppendPrintf(sentry, "icp.pkts_sent = %.0f\n",
                      stats->icp_pkts_sent);
    storeAppendPrintf(sentry, "icp.pkts_recv = %.0f\n",
                      stats->icp_pkts_recv);
    storeAppendPrintf(sentry, "icp.queries_sent = %.0f\n",
                      stats->icp_queries_sent);
    storeAppendPrintf(sentry, "icp.replies_sent = %.0f\n",
                      stats->icp_replies_sent);
    storeAppendPrintf(sentry, "icp.queries_recv = %.0f\n",
                      stats->icp_queries_recv);
    storeAppendPrintf(sentry, "icp.replies_recv = %.0f\n",
                      stats->icp_replies_recv);
    storeAppendPrintf(sentry, "icp.query_timeouts = %.0f\n",
                      stats->icp_query_timeouts);
    storeAppendPrintf(sentry, "icp.replies_queued = %.0f\n",
                      stats->icp_replies_queued);
    storeAppendPrintf(sentry, "icp.kbytes_sent = %.0f\n",
                      stats->icp_kbytes_sent);
    storeAppendPrintf(sentry, "icp.kbytes_recv = %.0f\n",
                      stats->icp_kbytes_recv);
    storeAppendPrintf(sentry, "icp.q_kbytes_sent = %.0f\n",
                      stats->icp_q_kbytes_sent);
    storeAppendPrintf(sentry, "icp.r_kbytes_sent = %.0f\n",
                      stats->icp_r_kbytes_sent);
    storeAppendPrintf(sentry, "icp.q_kbytes_recv = %.0f\n",
                      stats->icp_q_kbytes_recv);
    storeAppendPrintf(sentry, "icp.r_kbytes_recv = %.0f\n",
                      stats->icp_r_kbytes_recv);

#if USE_CACHE_DIGESTS

    storeAppendPrintf(sentry, "icp.times_used = %.0f\n",
                      stats->icp_times_used);
    storeAppendPrintf(sentry, "cd.times_used = %.0f\n",
                      stats->cd_times_used);
    storeAppendPrintf(sentry, "cd.msgs_sent = %.0f\n",
                      stats->cd_msgs_sent);
    storeAppendPrintf(sentry, "cd.msgs_recv = %.0f\n",
                      stats->cd_msgs_recv);
    storeAppendPrintf(sentry, "cd.memory = %.0f\n",
                      stats->cd_memory);
    storeAppendPrintf(sentry, "cd.local_memory = %.0f\n",
                      stats->cd_local_memory);
    storeAppendPrintf(sentry, "cd.kbytes_sent = %.0f\n",
                      stats->cd_kbytes_sent);
    storeAppendPrintf(sentry, "cd.kbytes_recv = %.0f\n",
                      stats->cd_kbytes_recv);
#endif

    storeAppendPrintf(sentry, "unlink.requests = %.0f\n",
                      stats->unlink_requests);
    storeAppendPrintf(sentry, "page_faults = %.0f\n",
                      stats->page_faults);
#if 0	
    storeAppendPrintf(sentry, "select_loops = %.0f\n",
                      stats->select_loops);
#endif	
    storeAppendPrintf(sentry, "cpu_time = %f\n",
                      stats->cpu_time);
    storeAppendPrintf(sentry, "wall_time = %f\n",
                      stats->wall_time);
    storeAppendPrintf(sentry, "swap.outs = %.0f\n",
                      stats->swap_outs);
    storeAppendPrintf(sentry, "swap.ins = %.0f\n",
                      stats->swap_ins);
    storeAppendPrintf(sentry, "swap.files_cleaned = %.0f\n",
                      stats->swap_files_cleaned);
    storeAppendPrintf(sentry, "aborted_requests = %.0f\n",
                      stats->aborted_requests);
}

static void
statCountersDump(StoreEntry * sentry, void* data)
{
    StatCounters *f = &statCounter;
    struct rusage rusage;
    squid_getrusage(&rusage);
    f->page_faults = rusage_pagefaults(&rusage);
    f->cputime = rusage_cputime(&rusage);

    storeAppendPrintf(sentry, "sample_time = %d.%d (%s)\n",
	(int) f->timestamp.tv_sec,
	(int) f->timestamp.tv_usec,
	mkrfc1123(f->timestamp.tv_sec));
    storeAppendPrintf(sentry, "client_http.requests = %d\n",
	f->client_http.requests);
    storeAppendPrintf(sentry, "client_http.hits = %d\n",
	f->client_http.hits);
    storeAppendPrintf(sentry, "client_http.errors = %d\n",
	f->client_http.errors);
    storeAppendPrintf(sentry, "client_http.kbytes_in = %d\n",
	(int) f->client_http.kbytes_in.kb);
    storeAppendPrintf(sentry, "client_http.kbytes_out = %d\n",
	(int) f->client_http.kbytes_out.kb);
    storeAppendPrintf(sentry, "client_http.hit_kbytes_out = %d\n",
	(int) f->client_http.hit_kbytes_out.kb);

    storeAppendPrintf(sentry, "server.all.requests = %d\n",
	(int) f->server.all.requests);
    storeAppendPrintf(sentry, "server.all.errors = %d\n",
	(int) f->server.all.errors);
    storeAppendPrintf(sentry, "server.all.kbytes_in = %d\n",
	(int) f->server.all.kbytes_in.kb);
    storeAppendPrintf(sentry, "server.all.kbytes_out = %d\n",
	(int) f->server.all.kbytes_out.kb);

    storeAppendPrintf(sentry, "server.http.requests = %d\n",
	(int) f->server.http.requests);
    storeAppendPrintf(sentry, "server.http.errors = %d\n",
	(int) f->server.http.errors);
    storeAppendPrintf(sentry, "server.http.kbytes_in = %d\n",
	(int) f->server.http.kbytes_in.kb);
    storeAppendPrintf(sentry, "server.http.kbytes_out = %d\n",
	(int) f->server.http.kbytes_out.kb);

    storeAppendPrintf(sentry, "server.ftp.requests = %d\n",
	(int) f->server.ftp.requests);
    storeAppendPrintf(sentry, "server.ftp.errors = %d\n",
	(int) f->server.ftp.errors);
    storeAppendPrintf(sentry, "server.ftp.kbytes_in = %d\n",
	(int) f->server.ftp.kbytes_in.kb);
    storeAppendPrintf(sentry, "server.ftp.kbytes_out = %d\n",
	(int) f->server.ftp.kbytes_out.kb);

    storeAppendPrintf(sentry, "server.other.requests = %d\n",
	(int) f->server.other.requests);
    storeAppendPrintf(sentry, "server.other.errors = %d\n",
	(int) f->server.other.errors);
    storeAppendPrintf(sentry, "server.other.kbytes_in = %d\n",
	(int) f->server.other.kbytes_in.kb);
    storeAppendPrintf(sentry, "server.other.kbytes_out = %d\n",
	(int) f->server.other.kbytes_out.kb);

    storeAppendPrintf(sentry, "icp.pkts_sent = %d\n",
	f->icp.pkts_sent);
    storeAppendPrintf(sentry, "icp.pkts_recv = %d\n",
	f->icp.pkts_recv);
    storeAppendPrintf(sentry, "icp.queries_sent = %d\n",
	f->icp.queries_sent);
    storeAppendPrintf(sentry, "icp.replies_sent = %d\n",
	f->icp.replies_sent);
    storeAppendPrintf(sentry, "icp.queries_recv = %d\n",
	f->icp.queries_recv);
    storeAppendPrintf(sentry, "icp.replies_recv = %d\n",
	f->icp.replies_recv);
    storeAppendPrintf(sentry, "icp.query_timeouts = %d\n",
	f->icp.query_timeouts);
    storeAppendPrintf(sentry, "icp.replies_queued = %d\n",
	f->icp.replies_queued);
    storeAppendPrintf(sentry, "icp.kbytes_sent = %d\n",
	(int) f->icp.kbytes_sent.kb);
    storeAppendPrintf(sentry, "icp.kbytes_recv = %d\n",
	(int) f->icp.kbytes_recv.kb);
    storeAppendPrintf(sentry, "icp.q_kbytes_sent = %d\n",
	(int) f->icp.q_kbytes_sent.kb);
    storeAppendPrintf(sentry, "icp.r_kbytes_sent = %d\n",
	(int) f->icp.r_kbytes_sent.kb);
    storeAppendPrintf(sentry, "icp.q_kbytes_recv = %d\n",
	(int) f->icp.q_kbytes_recv.kb);
    storeAppendPrintf(sentry, "icp.r_kbytes_recv = %d\n",
	(int) f->icp.r_kbytes_recv.kb);

#if USE_CACHE_DIGESTS
    storeAppendPrintf(sentry, "icp.times_used = %d\n",
	f->icp.times_used);
    storeAppendPrintf(sentry, "cd.times_used = %d\n",
	f->cd.times_used);
    storeAppendPrintf(sentry, "cd.msgs_sent = %d\n",
	f->cd.msgs_sent);
    storeAppendPrintf(sentry, "cd.msgs_recv = %d\n",
	f->cd.msgs_recv);
    storeAppendPrintf(sentry, "cd.memory = %d\n",
	(int) f->cd.memory.kb);
    storeAppendPrintf(sentry, "cd.local_memory = %d\n",
	(int) (store_digest ? store_digest->mask_size / 1024 : 0));
    storeAppendPrintf(sentry, "cd.kbytes_sent = %d\n",
	(int) f->cd.kbytes_sent.kb);
    storeAppendPrintf(sentry, "cd.kbytes_recv = %d\n",
	(int) f->cd.kbytes_recv.kb);
#endif

    storeAppendPrintf(sentry, "unlink.requests = %d\n",
	f->unlink.requests);
    storeAppendPrintf(sentry, "page_faults = %d\n",
	f->page_faults);
#if 0
    storeAppendPrintf(sentry, "select_loops = %d\n",
	f->select_loops);
#endif
    storeAppendPrintf(sentry, "cpu_time = %f\n",
	f->cputime);
    storeAppendPrintf(sentry, "wall_time = %f\n",
	tvSubDsec(f->timestamp, current_time));
    storeAppendPrintf(sentry, "swap.outs = %d\n",
	f->swap.outs);
    storeAppendPrintf(sentry, "swap.ins = %d\n",
	f->swap.ins);
    storeAppendPrintf(sentry, "swap.files_cleaned = %d\n",
	f->swap.files_cleaned);
    storeAppendPrintf(sentry, "aborted_requests = %d\n",
	f->aborted_requests);
}

static void
statCountersDumpEx(StoreEntry * sentry, void* data)
{
	if(data)
	{
		DumpCountersStats(sentry, data);
	}
	else
	{
		statCountersDump(sentry,data);
	}
}

void
statFreeMemory(void)
{
    int i;
    for (i = 0; i < N_COUNT_HIST; i++)
	statCountersClean(&CountHist[i]);
    for (i = 0; i < N_COUNT_HOUR_HIST; i++)
	statCountersClean(&CountHourHist[i]);
}

static void
statPeerSelect(StoreEntry * sentry, void* data)
{
#if USE_CACHE_DIGESTS
    StatCounters *f = &statCounter;
    peer *peer;
    const int tot_used = f->cd.times_used + f->icp.times_used;

    /* totals */
    cacheDigestGuessStatsReport(&f->cd.guess, sentry, "all peers");
    /* per-peer */
    storeAppendPrintf(sentry, "\nPer-peer statistics:\n");
    for (peer = getFirstPeer(); peer; peer = getNextPeer(peer)) {
	if (peer->digest)
	    peerDigestStatsReport(peer->digest, sentry);
	else
	    storeAppendPrintf(sentry, "\nNo peer digest from %s\n", peer->host);
	storeAppendPrintf(sentry, "\n");
    }

    storeAppendPrintf(sentry, "\nAlgorithm usage:\n");
    storeAppendPrintf(sentry, "Cache Digest: %7d (%3d%%)\n",
	f->cd.times_used, xpercentInt(f->cd.times_used, tot_used));
    storeAppendPrintf(sentry, "Icp:          %7d (%3d%%)\n",
	f->icp.times_used, xpercentInt(f->icp.times_used, tot_used));
    storeAppendPrintf(sentry, "Total:        %7d (%3d%%)\n",
	tot_used, xpercentInt(tot_used, tot_used));
#else
    storeAppendPrintf(sentry, "peer digests are disabled; no stats is available.\n");
#endif
}

static void
statDigestBlob(StoreEntry * sentry, void* data)
{
    storeAppendPrintf(sentry, "\nCounters:\n");
    statCountersDump(sentry,data);
    storeAppendPrintf(sentry, "\n5 Min Averages:\n");
    statAvgDump(sentry, 5, 0);
    storeAppendPrintf(sentry, "\nHistograms:\n");
    statCountersHistograms(sentry,data);
    storeAppendPrintf(sentry, "\nPeer Digests:\n");
    statPeerSelect(sentry,data);
    storeAppendPrintf(sentry, "\nLocal Digest:\n");
    storeDigestReport(sentry,data);
}

static void
statCurrentStuff(StoreEntry *e, void* data)
{
	storeAppendPrintf(e, "info.client_side.conn_count=%d\n", connStateGetCount());
	storeAppendPrintf(e, "info.http.conn_count=%d\n", httpGetCount());
	storeAppendPrintf(e, "info.clientdb.num_clients=%u\n", statCounter.client_http.clients);
	storeAppendPrintf(e, "info.client_side.num_http_requests=%u\n", statCounter.client_http.requests);
	storeAppendPrintf(e, "info.icp.num_icp_received=%u\n", statCounter.icp.pkts_recv);
	storeAppendPrintf(e, "info.icp.num_icp_sent=%u\n", statCounter.icp.pkts_sent);
	storeAppendPrintf(e, "info.icp.num_icp_replies=%u\n", statCounter.icp.replies_queued);
	storeAppendPrintf(e, "info.client_side.request_failure_ratio=%.2f\n", request_failure_ratio);

	storeAppendPrintf(e, "info.client_side.req.hit_ratio.5min=%3.1f\n", statRequestHitRatio(5));
	storeAppendPrintf(e, "info.client_side.req.hit_ratio.60min=%3.1f\n", statRequestHitRatio(60));

	storeAppendPrintf(e, "info.client_side.byte.hit_ratio.5min=%3.1f\n", statByteHitRatio(5));
	storeAppendPrintf(e, "info.client_side.byte.hit_ratio.60min=%3.1f\n", statByteHitRatio(60));

	storeAppendPrintf(e, "info.client_side.mem.hit_ratio.5min=%3.1f\n", statRequestHitMemoryRatio(5));
	storeAppendPrintf(e, "info.client_side.mem.hit_ratio.60min=%3.1f\n", statRequestHitMemoryRatio(60));

	storeAppendPrintf(e, "info.client_side.disk.hit_ratio.5min=%3.1f\n", statRequestHitDiskRatio(5));
	storeAppendPrintf(e, "info.client_side.disk.hit_ratio.60min=%3.1f\n", statRequestHitDiskRatio(60));

	storeAppendPrintf(e, "info.store.disk.sizekb=%d\n", store_swap_size);
	storeAppendPrintf(e, "info.store.mem.sizekb=%d\n", (int) (store_mem_size >> 10));

	storeAppendPrintf(e, "info.fd.max_count=%d\n", Squid_MaxFD);
	storeAppendPrintf(e, "info.fd.largest_count=%d\n", Biggest_FD);
	storeAppendPrintf(e, "info.fd.current_count=%d\n", Number_FD);

}

static void
statAvg5min(StoreEntry * e, void* data)
{
    statAvgDump(e, 5, 0);
}


static void
statAvg60min(StoreEntry * e, void* data)
{
    statAvgDump(e, 60, 0);
}

static void*
getAvg5min()
{
	return getAvgStat(5,0);
}

static void*	
getAvg60min()
{
	return getAvgStat(60,0);
}


static void
statAvg5minEx(StoreEntry * e, void* data)
{
	if(data)
	{
		DumpAvgStat(e,data);
	}
	else
	{
		statAvgDump(e, 5, 0);
	}
}

static void
statAvg60minEx(StoreEntry * e, void* data)
{
	if(data)
	{
		DumpAvgStat(e,data);
	}
	else
	{
    	statAvgDump(e, 60, 0);
	}
}


static double
statMedianSvc(int interval, int which)
{
    StatCounters *f;
    StatCounters *l;
    double x;
    assert(interval > 0);
    if (interval > N_COUNT_HIST - 1)
	interval = N_COUNT_HIST - 1;
    f = &CountHist[0];
    l = &CountHist[interval];
    assert(f);
    assert(l);
    switch (which) {
    case MEDIAN_HTTP:
	x = statHistDeltaMedian(&l->client_http.all_svc_time, &f->client_http.all_svc_time);
	break;
    case MEDIAN_HIT:
	x = statHistDeltaMedian(&l->client_http.hit_svc_time, &f->client_http.hit_svc_time);
	break;
    case MEDIAN_MISS:
	x = statHistDeltaMedian(&l->client_http.miss_svc_time, &f->client_http.miss_svc_time);
	break;
    case MEDIAN_NM:
	x = statHistDeltaMedian(&l->client_http.nm_svc_time, &f->client_http.nm_svc_time);
	break;
    case MEDIAN_NH:
	x = statHistDeltaMedian(&l->client_http.nh_svc_time, &f->client_http.nh_svc_time);
	break;
    case MEDIAN_ICP_QUERY:
	x = statHistDeltaMedian(&l->icp.query_svc_time, &f->icp.query_svc_time);
	break;
    case MEDIAN_DNS:
	x = statHistDeltaMedian(&l->dns.svc_time, &f->dns.svc_time);
	break;
    default:
	debugs(49, 5, "statMedianSvc: unknown type.");
	x = 0;
    }
    return x;
}

/*
 * SNMP wants ints, ick
 */
#if UNUSED_CODE
int
get_median_svc(int interval, int which)
{
    return (int) statMedianSvc(interval, which);
}

#endif

StatCounters *
snmpStatGet(int minutes)
{
    return &CountHist[minutes];
}

int
stat5minClientRequests(void)
{
    assert(N_COUNT_HIST > 5);
    return statCounter.client_http.requests - CountHist[5].client_http.requests;
}

static double
statCPUUsage(int minutes)
{
    assert(minutes < N_COUNT_HIST);
    return dpercent(CountHist[0].cputime - CountHist[minutes].cputime,
	tvSubDsec(CountHist[minutes].timestamp, CountHist[0].timestamp));
}

extern double
statRequestHitRatio(int minutes)
{
    assert(minutes < N_COUNT_HIST);
    return dpercent(CountHist[0].client_http.hits -
	CountHist[minutes].client_http.hits,
	CountHist[0].client_http.requests -
	CountHist[minutes].client_http.requests);
}

extern double
statRequestHitMemoryRatio(int minutes)
{
    assert(minutes < N_COUNT_HIST);
    return dpercent(CountHist[0].client_http.mem_hits -
	CountHist[minutes].client_http.mem_hits,
	CountHist[0].client_http.hits -
	CountHist[minutes].client_http.hits);
}

extern double
statRequestHitDiskRatio(int minutes)
{
    assert(minutes < N_COUNT_HIST);
    return dpercent(CountHist[0].client_http.disk_hits -
	CountHist[minutes].client_http.disk_hits,
	CountHist[0].client_http.hits -
	CountHist[minutes].client_http.hits);
}

extern double
statByteHitRatio(int minutes)
{
    size_t s;
    size_t c;
#if USE_CACHE_DIGESTS
    size_t cd;
#endif
    /* size_t might be unsigned */
    assert(minutes < N_COUNT_HIST);
    c = CountHist[0].client_http.kbytes_out.kb - CountHist[minutes].client_http.kbytes_out.kb;
    s = CountHist[0].server.all.kbytes_in.kb - CountHist[minutes].server.all.kbytes_in.kb;
#if USE_CACHE_DIGESTS
    /*
     * This ugly hack is here to prevent the user from seeing a
     * negative byte hit ratio.  When we fetch a cache digest from
     * a neighbor, it gets treated like a cache miss because the
     * object is consumed internally.  Thus, we subtract cache
     * digest bytes out before calculating the byte hit ratio.
     */
    cd = CountHist[0].cd.kbytes_recv.kb - CountHist[minutes].cd.kbytes_recv.kb;
    if (s < cd)
	debugs(18, 1, "STRANGE: srv_kbytes=%d, cd_kbytes=%d", (int) s, (int) cd);
    s -= cd;
#endif
    if (c > s)
	return dpercent(c - s, c);
    else
	return (-1.0 * dpercent(s - c, c));
}

static void
statClientRequests(StoreEntry * s, void* data)
{
    dlink_node *i;
    clientHttpRequest *http;
    ConnStateData *conn;
    StoreEntry *e;
    int fd;
    for (i = ClientActiveRequests.head; i; i = i->next) {
	const char *p = NULL;
	http = i->data;
	assert(http);
	conn = http->conn;
	storeAppendPrintf(s, "Connection: %p\n", conn);
	if (conn) {
	    fd = conn->fd;
	    storeAppendPrintf(s, "\tFD %d, read %" PRINTF_OFF_T ", wrote %" PRINTF_OFF_T "\n", fd,
		fd_table[fd].bytes_read, fd_table[fd].bytes_written);
	    storeAppendPrintf(s, "\tFD desc: %s\n", fd_table[fd].desc);
	    storeAppendPrintf(s, "\tin: buf %p, offset %ld, size %ld\n",
		conn->in.buf, (long int) conn->in.offset, (long int) conn->in.size);
	    storeAppendPrintf(s, "\tpeer: %s:%d\n",
		inet_ntoa(conn->peer.sin_addr),
		ntohs(conn->peer.sin_port));
	    storeAppendPrintf(s, "\tme: %s:%d\n",
		inet_ntoa(conn->me.sin_addr),
		ntohs(conn->me.sin_port));
	    storeAppendPrintf(s, "\tnrequests: %d\n",
		conn->nrequests);
	    storeAppendPrintf(s, "\tdefer: n %d, until %ld\n",
		conn->defer.n, (long int) conn->defer.until);
	}
	storeAppendPrintf(s, "uri %s\n", http->uri);
	storeAppendPrintf(s, "log_type %s\n", log_tags[http->log_type]);
	storeAppendPrintf(s, "out.offset %ld, out.size %lu\n",
	    (long int) http->out.offset, (unsigned long int) http->out.size);
	storeAppendPrintf(s, "req_sz %ld\n", (long int) http->req_sz);
	e = http->entry;
	storeAppendPrintf(s, "entry %p/%s\n", e, e ? storeKeyText(e->hash.key) : "N/A");
	e = http->old_entry;
	storeAppendPrintf(s, "old_entry %p/%s\n", e, e ? storeKeyText(e->hash.key) : "N/A");
	storeAppendPrintf(s, "start %ld.%06d (%f seconds ago)\n",
	    (long int) http->start.tv_sec,
	    (int) http->start.tv_usec,
	    tvSubDsec(http->start, current_time));
	if (http->request->auth_user_request)
	    p = authenticateUserRequestUsername(http->request->auth_user_request);
	else if (http->request->extacl_user) {
	    p = http->request->extacl_user;
	}
	if (!p && conn->rfc931[0])
	    p = conn->rfc931;
#if USE_SSL
	if (!p)
	    p = sslGetUserEmail(fd_table[conn->fd].ssl);
#endif
	if (!p)
	    p = dash_str;
	storeAppendPrintf(s, "username %s\n", p);
#if DELAY_POOLS
	if (http->sc) {
	    int pool = (http->sc->delay_id >> 16);
	    storeAppendPrintf(s, "active delay_pool %d\n", pool);
	    if (http->delayMaxBodySize > 0)
		storeAppendPrintf(s, "delayed delay_pool %d; transfer threshold %" PRINTF_OFF_T " bytes\n",
		    http->delayAssignedPool,
		    http->delayMaxBodySize);
	}
#endif
	storeAppendPrintf(s, "\n");
    }
}

#if STAT_GRAPHS
/*
 * urgh, i don't like these, but they do cut the amount of code down immensely
 */

#define GRAPH_PER_MIN(Y) \
    for (i=0;i<(N_COUNT_HIST-2);i++) { \
	dt = tvSubDsec(CountHist[i].timestamp, CountHist[i+1].timestamp); \
	if (dt <= 0.0) \
	    break; \
	storeAppendPrintf(e, "%lu,%0.2f:", \
	    CountHist[i].timestamp.tv_sec, \
	    ((CountHist[i].Y - CountHist[i+1].Y) / dt)); \
    }

#define GRAPH_PER_HOUR(Y) \
    for (i=0;i<(N_COUNT_HOUR_HIST-2);i++) { \
	dt = tvSubDsec(CountHourHist[i].timestamp, CountHourHist[i+1].timestamp); \
	if (dt <= 0.0) \
	    break; \
	storeAppendPrintf(e, "%lu,%0.2f:", \
	    CountHourHist[i].timestamp.tv_sec, \
	    ((CountHourHist[i].Y - CountHourHist[i+1].Y) / dt)); \
    }

#define GRAPH_TITLE(X,Y) storeAppendPrintf(e,"%s\t%s\t",X,Y);
#define GRAPH_END storeAppendPrintf(e,"\n");

#define GENGRAPH(X,Y,Z) \
    GRAPH_TITLE(Y,Z) \
    GRAPH_PER_MIN(X) \
    GRAPH_PER_HOUR(X) \
    GRAPH_END

static void
statGraphDump(StoreEntry * e, void* data)
{
    int i;
    double dt;

    GENGRAPH(client_http.requests, "client_http.requests", "Client HTTP requests/sec");
    GENGRAPH(client_http.hits, "client_http.hits", "Client HTTP hits/sec");
    GENGRAPH(client_http.errors, "client_http.errors", "Client HTTP errors/sec");
    GENGRAPH(client_http.kbytes_in.kb, "client_http.kbytes_in", "Client HTTP kbytes_in/sec");
    GENGRAPH(client_http.kbytes_out.kb, "client_http.kbytes_out", "Client HTTP kbytes_out/sec");

    /* XXX todo: http median service times */

    GENGRAPH(server.all.requests, "server.all.requests", "Server requests/sec");
    GENGRAPH(server.all.errors, "server.all.errors", "Server errors/sec");
    GENGRAPH(server.all.kbytes_in.kb, "server.all.kbytes_in", "Server total kbytes_in/sec");
    GENGRAPH(server.all.kbytes_out.kb, "server.all.kbytes_out", "Server total kbytes_out/sec");

    GENGRAPH(server.http.requests, "server.http.requests", "Server HTTP requests/sec");
    GENGRAPH(server.http.errors, "server.http.errors", "Server HTTP errors/sec");
    GENGRAPH(server.http.kbytes_in.kb, "server.http.kbytes_in", "Server HTTP kbytes_in/sec");
    GENGRAPH(server.http.kbytes_out.kb, "server.http.kbytes_out", "Server HTTP kbytes_out/sec");

    GENGRAPH(server.ftp.requests, "server.ftp.requests", "Server FTP requests/sec");
    GENGRAPH(server.ftp.errors, "server.ftp.errors", "Server FTP errors/sec");
    GENGRAPH(server.ftp.kbytes_in.kb, "server.ftp.kbytes_in", "Server FTP kbytes_in/sec");
    GENGRAPH(server.ftp.kbytes_out.kb, "server.ftp.kbytes_out", "Server FTP kbytes_out/sec");

    GENGRAPH(server.other.requests, "server.other.requests", "Server other requests/sec");
    GENGRAPH(server.other.errors, "server.other.errors", "Server other errors/sec");
    GENGRAPH(server.other.kbytes_in.kb, "server.other.kbytes_in", "Server other kbytes_in/sec");
    GENGRAPH(server.other.kbytes_out.kb, "server.other.kbytes_out", "Server other kbytes_out/sec");

    GENGRAPH(icp.pkts_sent, "icp.pkts_sent", "ICP packets sent/sec");
    GENGRAPH(icp.pkts_recv, "icp.pkts_recv", "ICP packets received/sec");
    GENGRAPH(icp.kbytes_sent.kb, "icp.kbytes_sent", "ICP kbytes_sent/sec");
    GENGRAPH(icp.kbytes_recv.kb, "icp.kbytes_recv", "ICP kbytes_received/sec");

    /* XXX todo: icp median service times */
    /* XXX todo: dns median service times */

    GENGRAPH(unlink.requests, "unlink.requests", "Cache File unlink requests/sec");
    GENGRAPH(page_faults, "page_faults", "System Page Faults/sec");
    GENGRAPH(select_loops, "select_loops", "System Select Loop calls/sec");
    GENGRAPH(cputime, "cputime", "CPU utilisation");
}

#endif /* STAT_GRAPHS */

size_t
statMemoryAccounted(void)
{
    return memTotalAllocated();
}

/*
 * Only if asyncio is compiled in
 */
void
aioStats(StoreEntry * sentry, void* data)
{
    squidaio_thread_t *threadp;
    int i;

    storeAppendPrintf(sentry, "ASYNC IO Counters:\n");
    storeAppendPrintf(sentry, "Operation\t# Requests\n");
    storeAppendPrintf(sentry, "open\t%d\n", squidaio_counts.open);
    storeAppendPrintf(sentry, "close\t%d\n", squidaio_counts.close);
    storeAppendPrintf(sentry, "cancel\t%d\n", squidaio_counts.cancel);
    storeAppendPrintf(sentry, "write\t%d\n", squidaio_counts.write);
    storeAppendPrintf(sentry, "read\t%d\n", squidaio_counts.read);
    storeAppendPrintf(sentry, "stat\t%d\n", squidaio_counts.stat);
    storeAppendPrintf(sentry, "unlink\t%d\n", squidaio_counts.unlink);
    storeAppendPrintf(sentry, "check_callback\t%d\n", squidaio_counts.check_callback);
    storeAppendPrintf(sentry, "queue\t%d\n", squidaio_get_queue_len());


    storeAppendPrintf(sentry, "\n\nThreads Status:\n");
    storeAppendPrintf(sentry, "#\tID\t# Requests\n");

    threadp = squidaio_get_thread_head();
    for (i = 0; i < squidaio_nthreads; i++) {
        storeAppendPrintf(sentry, "%i\t0x%lx\t%ld\n", i + 1, (long int) threadp->thread, threadp->requests);
        threadp = threadp->next;
    }
}

