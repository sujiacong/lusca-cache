
/*
 * $Id: store_dir_coss.c 14248 2009-07-27 03:14:38Z adrian.chadd $
 *
 * DEBUG: section 47    Store COSS Directory Routines
 * AUTHOR: Eric Stern
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

#include "../../libasyncio/aiops.h"
#include "../../libasyncio/async_io.h"
#include "store_coss.h"
#include "store_rebuild_coss.h"
#include "store_log_coss.h"

#define STORE_META_BUFSZ 4096
#define HITONLY_BUFS 2

int max_coss_dir_size = 0;
static int last_coss_pick_index = -1;
int coss_initialised = 0;
MemPool *coss_state_pool = NULL;
MemPool *coss_index_pool = NULL;
MemPool *coss_realloc_pool = NULL;
MemPool *coss_op_pool = NULL;

static STINIT storeCossDirInit;
static STNEWFS storeCossDirNewfs;
static STCHECKOBJ storeCossDirCheckObj;
static STCHECKLOADAV storeCossDirCheckLoadAv;
static STFREE storeCossDirShutdown;
static STFSPARSE storeCossDirParse;
static STFSRECONFIGURE storeCossDirReconfigure;
static STDUMP storeCossDirDump;
static STCALLBACK storeCossDirCallback;
static void storeCossDirParseBlkSize(SwapDir *, const char *, const char *, int);
static void storeCossDirParseOverwritePct(SwapDir *, const char *, const char *, int);
static void storeCossDirParseMaxWaste(SwapDir *, const char *, const char *, int);
static void storeCossDirParseMemOnlyBufs(SwapDir *, const char *, const char *, int);
static void storeCossDirParseMaxFullBufs(SwapDir *, const char *, const char *, int);
static void storeCossDirDumpBlkSize(StoreEntry *, const char *, SwapDir *);
static void storeCossDirDumpOverwritePct(StoreEntry *, const char *, SwapDir *);
static void storeCossDirDumpMaxWaste(StoreEntry *, const char *, SwapDir *);
static void storeCossDirDumpMemOnlyBufs(StoreEntry *, const char *, SwapDir *);
static void storeCossDirDumpMaxFullBufs(StoreEntry *, const char *, SwapDir *);
static OBJH storeCossStats;

/* The "only" externally visible function */
STSETUP storeFsSetup_coss;

static struct cache_dir_option options[] =
{
    {"block-size", storeCossDirParseBlkSize, storeCossDirDumpBlkSize},
    {"overwrite-percent", storeCossDirParseOverwritePct, storeCossDirDumpOverwritePct},
    {"max-stripe-waste", storeCossDirParseMaxWaste, storeCossDirDumpMaxWaste},
    {"membufs", storeCossDirParseMemOnlyBufs, storeCossDirDumpMemOnlyBufs},
    {"maxfullbufs", storeCossDirParseMaxFullBufs, storeCossDirDumpMaxFullBufs},
    {NULL, NULL}
};

struct _coss_stats coss_stats;

char const *
stripePath(SwapDir * sd)
{
    CossInfo *cs = (CossInfo *) sd->fsdata;
    char pathtmp[SQUID_MAXPATHLEN];
    struct stat st;

    if (!cs->stripe_path) {
	strcpy(pathtmp, sd->path);
	if (stat(sd->path, &st) == 0) {
	    if (S_ISDIR(st.st_mode))
		strcat(pathtmp, "/stripe");
	} else
	    fatalf("stripePath: Cannot stat %s.", sd->path);
	cs->stripe_path = xstrdup(pathtmp);
    }
    return cs->stripe_path;
}

static void
storeCossDirInit(SwapDir * sd)
{
    CossInfo *cs = (CossInfo *) sd->fsdata;

    /* COSS is pretty useless without 64 bit file offsets */
    if (sizeof(off_t) < 8) {
	fatalf("COSS will not function without large file support (off_t is %d bytes long. Please reconsider recompiling squid with --with-large-files\n", (int) sizeof(off_t));
    }
    aioInit();
    if (Config.aiops.n_aiops_threads > -1)
        squidaio_nthreads = Config.aiops.n_aiops_threads;
    squidaio_init();
    cs->fd = file_open(stripePath(sd), O_RDWR | O_CREAT | O_BINARY);
    if (cs->fd < 0) {
	debug(79, 1) ("%s: %s\n", stripePath(sd), xstrerror());
	fatal("storeCossDirInit: Failed to open a COSS file.");
    }
    storeCossDirRebuild(sd);
    n_coss_dirs++;
    aiops_default_ndirs ++;
    /*
     * fs.blksize is normally determined by calling statvfs() etc,
     * but we just set it here.  It is used in accounting the
     * total store size, and is reported in cachemgr 'storedir'
     * page.
     */
    sd->fs.blksize = 1 << cs->blksz_bits;
    comm_quick_poll_required();
}

void
storeCossRemove(SwapDir * sd, StoreEntry * e)
{
    CossInfo *cs = (CossInfo *) sd->fsdata;
    int stripe;
#if 0
    debug(1, 1) ("storeCossRemove: %x: %d/%d\n", e, (int) e->swap_dirn, (e) e->swap_filen);
#endif
    CossIndexNode *coss_node = e->repl.data;
    /* Do what the LRU and HEAP repl policies do.. */
    if (e->repl.data == NULL) {
	return;
    }
    assert(sd->index == e->swap_dirn);
    assert(e->swap_filen >= 0);
    e->repl.data = NULL;
    stripe = storeCossFilenoToStripe(cs, e->swap_filen);
    dlinkDelete(&coss_node->node, &cs->stripes[stripe].objlist);
    memPoolFree(coss_index_pool, coss_node);
    cs->count -= 1;
}

void
storeCossAdd(SwapDir * sd, StoreEntry * e, int curstripe)
{
    CossInfo *cs = (CossInfo *) sd->fsdata;
    CossStripe *cstripe = &cs->stripes[curstripe];
    CossIndexNode *coss_node = memPoolAlloc(coss_index_pool);
    assert(!e->repl.data);
    assert(sd->index == e->swap_dirn);
    /* Make sure the object exists in the current stripe, it should do! */
    assert(curstripe == storeCossFilenoToStripe(cs, e->swap_filen));
    e->repl.data = coss_node;
    dlinkAddTail(e, &coss_node->node, &cstripe->objlist);
    cs->count += 1;
}

static void
storeCossCreateStripe(SwapDir * SD, const char *path)
{
    char *block;
    int swap;
    int i;
    CossInfo *cs = (CossInfo *) SD->fsdata;

    debug(47, 1) ("Creating COSS stripe %s\n", path);
    swap = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0600);
    block = (char *) xcalloc(COSS_MEMBUF_SZ, 1);
    for (i = 0; i < cs->numstripes; ++i) {
	if (write(swap, block, COSS_MEMBUF_SZ) < COSS_MEMBUF_SZ) {
	    fatalf("Failed to create COSS stripe %s\n", path);
	}
    }
    close(swap);
    xfree(block);
}

static void
storeCossDirNewfs(SwapDir * SD)
{
    struct stat st;

    if (stat(SD->path, &st) == 0) {
	if (S_ISDIR(st.st_mode)) {
	    if (stat(stripePath(SD), &st) != 0)
		storeCossCreateStripe(SD, stripePath(SD));
	}
    } else
	storeCossCreateStripe(SD, (const char *) SD->path);
}

/*
 * Only "free" the filesystem specific stuff here
 */
static void
storeCossDirFree(SwapDir * SD)
{
    CossInfo *cs = (CossInfo *) SD->fsdata;
    if (cs->swaplog_fd > -1) {
	file_close(cs->swaplog_fd);
	cs->swaplog_fd = -1;
    }
    xfree(cs->stripes);
    xfree(cs->memstripes);
    xfree(cs);
    SD->fsdata = NULL;		/* Will aid debugging... */

}

/* we are shutting down, flush all membufs to disk */
static void
storeCossDirShutdown(SwapDir * SD)
{
    CossInfo *cs = (CossInfo *) SD->fsdata;
    if (cs->fd == -1)
	return;
    debug(47, 1) ("COSS: %s: syncing\n", stripePath(SD));

    storeCossSync(SD);		/* This'll call a_file_syncqueue() or a aioSync() */
    /* XXX how should one close the pending ops on a given asyncio fd? */
    file_close(cs->fd);
    cs->fd = -1;
    xfree((void *) cs->stripe_path);

    if (cs->swaplog_fd > -1) {
	file_close(cs->swaplog_fd);
	cs->swaplog_fd = -1;
    }
    n_coss_dirs--;
}

/*
 * storeCossDirCheckObj
 *
 * This routine is called by storeDirSelectSwapDir to see if the given
 * object is able to be stored on this filesystem. COSS filesystems will
 * not store everything. We don't check for maxobjsize here since its
 * done by the upper layers.
 */
int
storeCossDirCheckObj(SwapDir * SD, const StoreEntry * e)
{
    CossInfo *cs = SD->fsdata;
    int objsize = objectLen(e) + e->mem_obj->swap_hdr_sz;
    /* Check if the object is a special object, we can't cache these */
    if (EBIT_TEST(e->flags, ENTRY_SPECIAL))
	return 0;
    if (cs->rebuild.rebuilding == 1)
	return 0;
    /* Check to see if the object is going to waste too much disk space */
    if (objsize > cs->sizerange_max)
	return 0;

    return 1;
}

int
storeCossDirCheckLoadAv(SwapDir * SD, store_op_t op)
{
    CossInfo *cs = (CossInfo *) SD->fsdata;
    float disk_size_weight, current_write_weight;
    int cur_load_interval = (squid_curtime / cs->load_interval) % 2;
    int ql = 0;
    int loadav;

    /* Return load, cs->aq.aq_numpending out of MAX_ASYNCOP */
    ql = aioQueueSize();
    if (ql == 0)
	loadav = COSS_LOAD_BASE;
    else
	loadav = COSS_LOAD_BASE + (ql * COSS_LOAD_QUEUE_WEIGHT / MAGIC1);

    /* We want to try an keep the disks at a similar write rate 
     * otherwise the LRU algorithm breaks
     *
     * The queue length has a 10% weight on the load
     * The number of stripes written has a 90% weight
     */
    disk_size_weight = (float) max_coss_dir_size / SD->max_size;
    current_write_weight = (float) cs->loadcalc[cur_load_interval] * COSS_LOAD_STRIPE_WEIGHT / MAX_LOAD_VALUE;

    loadav += disk_size_weight * current_write_weight;

    /* Remove the folowing check if we want to allow COSS partitions to get
     * too busy to accept new objects
     */
    if (loadav > MAX_LOAD_VALUE)
	loadav = MAX_LOAD_VALUE;

    /* Finally, we want to reject all new obects if the number of full stripes
     * is too large
     */
    if (cs->numfullstripes > cs->hitonlyfullstripes)
	loadav += MAX_LOAD_VALUE;

    debug(47, 9) ("storeAufsDirCheckObj: load=%d\n", loadav);
    return loadav;
}


/*
 * storeCossDirCallback - do the IO completions
 */
static int
storeCossDirCallback(SwapDir * SD)
{
    CossInfo *cs = (CossInfo *) SD->fsdata;
    storeCossFreeDeadMemBufs(cs);
    /* There's no need to call aioCheckCallbacks() - this will happen through the aio notification pipe */
    return 0;
}

/* ========== LOCAL FUNCTIONS ABOVE, GLOBAL FUNCTIONS BELOW ========== */

static void
storeCossDirStats(SwapDir * SD, StoreEntry * sentry)
{
    CossInfo *cs = (CossInfo *) SD->fsdata;

    storeAppendPrintf(sentry, "\n");
    storeAppendPrintf(sentry, "Maximum Size: %d KB\n", SD->max_size);
    storeAppendPrintf(sentry, "Current Size: %d KB\n", SD->cur_size);
    storeAppendPrintf(sentry, "Percent Used: %0.2f%%\n",
	100.0 * SD->cur_size / SD->max_size);
    storeAppendPrintf(sentry, "Current load metric: %d / %d\n", storeCossDirCheckLoadAv(SD, ST_OP_CREATE), MAX_LOAD_VALUE);
    storeAppendPrintf(sentry, "Number of object collisions: %d\n", (int) cs->numcollisions);
#if 0
    /* is this applicable? I Hope not .. */
    storeAppendPrintf(sentry, "Filemap bits in use: %d of %d (%d%%)\n",
	SD->map->n_files_in_map, SD->map->max_n_files,
	percent(SD->map->n_files_in_map, SD->map->max_n_files));
#endif
    storeAppendPrintf(sentry, "Flags:");
    if (SD->flags.selected)
	storeAppendPrintf(sentry, " SELECTED");
    if (SD->flags.read_only)
	storeAppendPrintf(sentry, " READ-ONLY");
    storeAppendPrintf(sentry, "\n");
    storeAppendPrintf(sentry, "Pending Relocations: %d\n", cs->pending_reloc_count);
    storeAppendPrintf(sentry, "Current Stripe: %d\n", cs->curstripe);
    membufsDump(cs, sentry);
}

static void
storeCossDirParse(SwapDir * sd, int index, char *path)
{
    unsigned int i;
    unsigned int size;
    CossInfo *cs;
    off_t max_offset;

    i = GetInteger();
    size = i << 10;		/* Mbytes to Kbytes */
    if (size <= 0)
	fatal("storeCossDirParse: invalid size value");

    cs = xcalloc(1, sizeof(CossInfo));
    if (cs == NULL)
	fatal("storeCossDirParse: couldn't xmalloc() CossInfo!\n");

    sd->index = index;
    sd->path = xstrdup(path);
    sd->max_size = size;
    sd->fsdata = cs;

    cs->fd = -1;
    cs->swaplog_fd = -1;

    sd->init = storeCossDirInit;
    sd->newfs = storeCossDirNewfs;
    sd->dump = storeCossDirDump;
    sd->freefs = storeCossDirFree;
    sd->dblcheck = NULL;
    sd->statfs = storeCossDirStats;
    sd->maintainfs = NULL;
    sd->checkobj = storeCossDirCheckObj;
    sd->checkload = storeCossDirCheckLoadAv;
    sd->refobj = NULL;		/* LRU is done in storeCossRead */
    sd->unrefobj = NULL;
    sd->callback = storeCossDirCallback;
    sd->sync = storeCossSync;

    sd->obj.create = storeCossCreate;
    sd->obj.open = storeCossOpen;
    sd->obj.close = storeCossClose;
    sd->obj.read = storeCossRead;
    sd->obj.write = storeCossWrite;
    sd->obj.unlink = storeCossUnlink;
    sd->obj.recycle = storeCossRecycle;

    sd->log.open = storeCossDirOpenSwapLog;
    sd->log.close = storeCossDirCloseSwapLog;
    sd->log.write = storeCossDirSwapLog;

    sd->log.clean.start = NULL;
    sd->log.clean.write = NULL;
    sd->log.clean.nextentry = NULL;
    sd->log.clean.done = NULL;

    cs->current_offset = -1;
    cs->fd = -1;
    cs->swaplog_fd = -1;
    cs->numcollisions = 0;
    cs->membufs.head = cs->membufs.tail = NULL;		/* set when the rebuild completes */
    cs->current_membuf = NULL;
    cs->blksz_bits = 9;		/* default block size = 512 */
    cs->blksz_mask = (1 << cs->blksz_bits) - 1;

    /* By default, only overwrite objects that were written mor ethan 50% of the disk ago
     * and use a maximum of 10 in-memory stripes
     */
    cs->minumum_overwrite_pct = 0.5;
    cs->nummemstripes = 10;

    /* Calculate load in 60 second incremenets */
    /* This could be made configurable */
    cs->load_interval = 60;

    parse_cachedir_options(sd, options, 0);

    cs->sizerange_max = sd->max_objsize;
    cs->sizerange_min = sd->max_objsize;

    /* Enforce maxobjsize being set to something */
    if (sd->max_objsize == -1)
	fatal("COSS requires max-size to be set to something other than -1!\n");
    if (sd->max_objsize > COSS_MEMBUF_SZ)
	fatalf("COSS max-size option must be less than COSS_MEMBUF_SZ (%d)\n", COSS_MEMBUF_SZ);
    /*
     * check that we won't overflow sfileno later.  0xFFFFFF is the
     * largest possible sfileno, assuming sfileno is a 25-bit
     * signed integer, as defined in structs.h.
     */
    max_offset = (off_t) 0xFFFFFF << cs->blksz_bits;
    if ((sd->max_size + (cs->nummemstripes * (COSS_MEMBUF_SZ >> 10))) > (unsigned long) (max_offset >> 10)) {
	debug(47, 1) ("COSS block-size = %d bytes\n", 1 << cs->blksz_bits);
	debug(47, 1) ("COSS largest file offset = %lu KB\n", (unsigned long) max_offset >> 10);
	debug(47, 1) ("COSS cache_dir size = %d KB\n", sd->max_size);
	fatal("COSS cache_dir size exceeds largest offset\n");
    }
    cs->max_disk_nf = ((off_t) sd->max_size << 10) >> cs->blksz_bits;
    debug(47, 2) ("COSS: max disk fileno is %d\n", cs->max_disk_nf);

    /* XXX todo checks */

    /* Ensure that off_t range can cover the max_size */

    /* Ensure that the max size IS a multiple of the membuf size, or things
     * will get very fruity near the end of the disk. */
    cs->numstripes = (off_t) (((off_t) sd->max_size) << 10) / COSS_MEMBUF_SZ;
    debug(47, 2) ("COSS: number of stripes: %d of %d bytes each\n", cs->numstripes, COSS_MEMBUF_SZ);
    cs->stripes = xcalloc(cs->numstripes, sizeof(struct _cossstripe));
    for (i = 0; i < cs->numstripes; i++) {
	cs->stripes[i].id = i;
	cs->stripes[i].membuf = NULL;
	cs->stripes[i].numdiskobjs = -1;
    }
    cs->minimum_stripe_distance = cs->numstripes * cs->minumum_overwrite_pct;

    /* Make sure cs->maxfull has a default value */
    if (cs->maxfullstripes == 0)
	cs->maxfullstripes = cs->numstripes;

    /* We will reject new objects (ie go into hit-only mode)
     * if there are <= 2 stripes available
     */
    cs->hitonlyfullstripes = cs->maxfullstripes - HITONLY_BUFS;

    debug(47, 2) ("COSS: number of memory-only stripes %d of %d bytes each\n", cs->nummemstripes, COSS_MEMBUF_SZ);
    cs->memstripes = xcalloc(cs->nummemstripes, sizeof(struct _cossstripe));
    for (i = 0; i < cs->nummemstripes; i++) {
	cs->memstripes[i].id = i;
	cs->memstripes[i].membuf = NULL;
	cs->memstripes[i].numdiskobjs = -1;
    }

    /* Update the max size (used for load calculations) */
    if (sd->max_size > max_coss_dir_size)
	max_coss_dir_size = sd->max_size;
}

static void
storeCossDirReconfigure(SwapDir * sd, int index, char *path)
{
    unsigned int i;
    unsigned int size;

    i = GetInteger();
    size = i << 10;		/* Mbytes to Kbytes */
    if (size <= 0)
	fatal("storeCossDirParse: invalid size value");

    if (size == sd->max_size)
	debug(3, 1) ("Cache COSS dir '%s' size remains unchanged at %d KB\n", path, size);
    else {
	debug(3, 1) ("Cache COSS dir '%s' size changed to %d KB\n", path, size);
	sd->max_size = size;
    }
    parse_cachedir_options(sd, options, 1);
    /* Enforce maxobjsize being set to something */
    if (sd->max_objsize == -1)
	fatal("COSS requires max-size to be set to something other than -1!\n");
}

void
storeCossDirDump(StoreEntry * entry, SwapDir * s)
{
    storeAppendPrintf(entry, " %d", s->max_size >> 10);
    dump_cachedir_options(entry, options, s);
}

static void
storeCossDirParseMaxFullBufs(SwapDir * sd, const char *name, const char *value, int reconfiguring)
{
    CossInfo *cs = sd->fsdata;
    int maxfull = atoi(value);
    if (maxfull <= HITONLY_BUFS)
	fatalf("COSS ERROR: There must be more than %d maxfullbufs\n", HITONLY_BUFS);
    if (maxfull > 500)
	fatal("COSS ERROR: Squid will likely use too much memory if it ever used 500MB worth of full buffers\n");
    cs->maxfullstripes = maxfull;
}

static void
storeCossDirParseMemOnlyBufs(SwapDir * sd, const char *name, const char *value, int reconfiguring)
{
    CossInfo *cs = sd->fsdata;
    int membufs = atoi(value);
    if (reconfiguring) {
	debug(47, 0) ("WARNING: cannot change COSS memory bufs Squid is running\n");
	return;
    }
    if (membufs < 2)
	fatal("COSS ERROR: There must be at least 2 membufs\n");
    if (membufs > 500)
	fatal("COSS ERROR: Squid will likely use too much memory if it ever used 500MB worth of buffers\n");
    cs->nummemstripes = membufs;
}

static void
storeCossDirParseMaxWaste(SwapDir * sd, const char *name, const char *value, int reconfiguring)
{
    CossInfo *cs = sd->fsdata;
    int waste = atoi(value);

    if (waste < 8192)
	fatal("COSS max-stripe-waste must be > 8192\n");
    if (waste > sd->max_objsize)
	debug(47, 1) ("storeCossDirParseMaxWaste: COSS max-stripe-waste can not be bigger than the max object size (%" PRINTF_OFF_T ")\n", sd->max_objsize);
    cs->sizerange_min = waste;
}

static void
storeCossDirParseOverwritePct(SwapDir * sd, const char *name, const char *value, int reconfiguring)
{
    CossInfo *cs = sd->fsdata;
    int pct = atoi(value);

    if (pct < 0)
	fatal("COSS overwrite percent must be > 0\n");
    if (pct > 100)
	fatal("COSS overwrite percent must be < 100\n");
    cs->minumum_overwrite_pct = (float) pct / 100;
    cs->minimum_stripe_distance = cs->numstripes * cs->minumum_overwrite_pct;
}

static void
storeCossDirParseBlkSize(SwapDir * sd, const char *name, const char *value, int reconfiguring)
{
    CossInfo *cs = sd->fsdata;
    int blksz = atoi(value);
    int check;
    int nbits;
    if (blksz == (1 << cs->blksz_bits))
	/* no change */
	return;
    if (reconfiguring) {
	debug(47, 0) ("WARNING: cannot change COSS block-size while Squid is running\n");
	return;
    }
    nbits = 0;
    check = blksz;
    while (check > 1) {
	nbits++;
	check >>= 1;
    }
    check = 1 << nbits;
    if (check != blksz)
	fatal("COSS block-size must be a power of 2\n");
    if (nbits > 13)
	fatal("COSS block-size must be 8192 or smaller\n");
    cs->blksz_bits = nbits;
    cs->blksz_mask = (1 << cs->blksz_bits) - 1;
}

static void
storeCossDirDumpMaxFullBufs(StoreEntry * e, const char *option, SwapDir * sd)
{
    CossInfo *cs = sd->fsdata;
    storeAppendPrintf(e, " maxfullbufs=%d MB", cs->maxfullstripes);
}

static void
storeCossDirDumpMemOnlyBufs(StoreEntry * e, const char *option, SwapDir * sd)
{
    CossInfo *cs = sd->fsdata;
    storeAppendPrintf(e, " membufs=%d MB", cs->nummemstripes);
}

static void
storeCossDirDumpMaxWaste(StoreEntry * e, const char *option, SwapDir * sd)
{
    CossInfo *cs = sd->fsdata;
    storeAppendPrintf(e, " max-stripe-waste=%d", cs->sizerange_min);
}

static void
storeCossDirDumpOverwritePct(StoreEntry * e, const char *option, SwapDir * sd)
{
    CossInfo *cs = sd->fsdata;
    storeAppendPrintf(e, " overwrite-percent=%d%%", (int) cs->minumum_overwrite_pct * 100);
}

static void
storeCossDirDumpBlkSize(StoreEntry * e, const char *option, SwapDir * sd)
{
    CossInfo *cs = sd->fsdata;
    storeAppendPrintf(e, " block-size=%d", 1 << cs->blksz_bits);
}

static SwapDir *
storeCossDirPick(void)
{
    int i, choosenext = 0;
    SwapDir *SD;

    if (n_coss_dirs == 0)
	return NULL;
    for (i = 0; i < Config.cacheSwap.n_configured; i++) {
	SD = &Config.cacheSwap.swapDirs[i];
	if (strcmp(SD->type, SWAPDIR_COSS) == 0) {
	    CossInfo *cs = (CossInfo *) SD->fsdata;
	    if (cs->fd != -1) {
		if ((last_coss_pick_index == -1) || (n_coss_dirs == 1)) {
		    last_coss_pick_index = i;
		    return SD;
		} else if (choosenext) {
		    last_coss_pick_index = i;
		    return SD;
		}
	    } else if (last_coss_pick_index == i) {
		choosenext = 1;
	    }
	}
    }
    return NULL;
}

/*
 * initial setup/done code
 */
static void
storeCossDirDone(void)
{
    int i, n_dirs = n_coss_dirs;

    for (i = 0; i < n_dirs; i++)
	storeCossDirShutdown(storeCossDirPick());
/* 
 * TODO : check if others memPoolDestroy() of COSS objects are needed here
 */
    memPoolDestroy(coss_state_pool);
    coss_initialised = 0;
}

static void
storeCossStats(StoreEntry * sentry)
{
    const char *tbl_fmt = "%10s %10d %10d %10d\n";
    storeAppendPrintf(sentry, "\n                   OPS     SUCCESS        FAIL\n");
    storeAppendPrintf(sentry, tbl_fmt,
	"open", coss_stats.open.ops, coss_stats.open.success, coss_stats.open.fail);
    storeAppendPrintf(sentry, tbl_fmt,
	"create", coss_stats.create.ops, coss_stats.create.success, coss_stats.create.fail);
    storeAppendPrintf(sentry, tbl_fmt,
	"close", coss_stats.close.ops, coss_stats.close.success, coss_stats.close.fail);
    storeAppendPrintf(sentry, tbl_fmt,
	"unlink", coss_stats.unlink.ops, coss_stats.unlink.success, coss_stats.unlink.fail);
    storeAppendPrintf(sentry, tbl_fmt,
	"read", coss_stats.read.ops, coss_stats.read.success, coss_stats.read.fail);
    storeAppendPrintf(sentry, tbl_fmt,
	"write", coss_stats.write.ops, coss_stats.write.success, coss_stats.write.fail);
    storeAppendPrintf(sentry, tbl_fmt,
	"s_write", coss_stats.stripe_write.ops, coss_stats.stripe_write.success, coss_stats.stripe_write.fail);
    storeAppendPrintf(sentry, "\n");
    storeAppendPrintf(sentry, "stripes:          %d\n", coss_stats.stripes);
    storeAppendPrintf(sentry, "dead_stripes:     %d\n", coss_stats.dead_stripes);
    storeAppendPrintf(sentry, "alloc.alloc:      %d\n", coss_stats.alloc.alloc);
    storeAppendPrintf(sentry, "alloc.realloc:    %d\n", coss_stats.alloc.realloc);
    storeAppendPrintf(sentry, "alloc.memalloc:   %d\n", coss_stats.alloc.memalloc);
    storeAppendPrintf(sentry, "alloc.collisions: %d\n", coss_stats.alloc.collisions);
    storeAppendPrintf(sentry, "disk_overflows:   %d\n", coss_stats.disk_overflows);
    storeAppendPrintf(sentry, "stripe_overflows: %d\n", coss_stats.stripe_overflows);
    storeAppendPrintf(sentry, "open_mem_hits:    %d\n", coss_stats.open_mem_hits);
    storeAppendPrintf(sentry, "open_mem_misses:  %d\n", coss_stats.open_mem_misses);
}

void
storeFsSetup_coss(storefs_entry_t * storefs)
{
    assert(!coss_initialised);

    storefs->parsefunc = storeCossDirParse;
    storefs->reconfigurefunc = storeCossDirReconfigure;
    storefs->donefunc = storeCossDirDone;
    coss_state_pool = memPoolCreate("COSS IO State data", sizeof(CossState));
    coss_index_pool = memPoolCreate("COSS index data", sizeof(CossIndexNode));
    coss_realloc_pool = memPoolCreate("COSS pending realloc", sizeof(CossPendingReloc));
    coss_op_pool = memPoolCreate("COSS pending operation", sizeof(CossReadOp));
    cachemgrRegister(SWAPDIR_COSS, "COSS Stats", storeCossStats, 0, 1);
    coss_initialised = 1;
}
