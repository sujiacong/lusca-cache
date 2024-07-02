
/*
 * $Id: store_dir_aufs.c 14645 2010-05-11 02:56:12Z adrian.chadd $
 *
 * DEBUG: section 47    Store Directory Routines
 * AUTHOR: Duane Wessels
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
#include "../../libsqstore/filemap.h"
#include "store_asyncufs.h"
#include "store_bitmap_aufs.h"
#include "store_rebuild_aufs.h"
#include "store_log_aufs.h"

#define DefaultLevelOneDirs     16
#define DefaultLevelTwoDirs     256
#define STORE_META_BUFSZ 4096

int n_asyncufs_dirs = 0;
static int *asyncufs_dir_index = NULL;
MemPool *squidaio_state_pool = NULL;
MemPool *aufs_qread_pool = NULL;
MemPool *aufs_qwrite_pool = NULL;
static int asyncufs_initialised = 0;

static char *storeAufsDirSwapSubDir(SwapDir *, int subdirn);
static int storeAufsDirCreateDirectory(const char *path, int);
static int storeAufsDirVerifyCacheDirs(SwapDir *);
static int storeAufsDirVerifyDirectory(const char *path);
static void storeAufsDirCreateSwapSubDirs(SwapDir *);

static STINIT storeAufsDirInit;
static STFREE storeAufsDirFree;

static STNEWFS storeAufsDirNewfs;
static STDUMP storeAufsDirDump;
static STMAINTAINFS storeAufsDirMaintain;
static STCHECKOBJ storeAufsDirCheckObj;
static STCHECKLOADAV storeAufsDirCheckLoadAv;
static STREFOBJ storeAufsDirRefObj;
/* static STUNREFOBJ storeAufsDirUnrefObj; */

static QS rev_int_sort;
static int storeAufsDirClean(int swap_index);
static EVH storeAufsDirCleanEvent;
static int storeAufsCleanupDoubleCheck(SwapDir *, StoreEntry *);
static void storeAufsDirStats(SwapDir *, StoreEntry *);
static void storeAufsSync(SwapDir *);

/* The MAIN externally visible function */
STSETUP storeFsSetup_aufs;

static char *
storeAufsDirSwapSubDir(SwapDir * sd, int subdirn)
{
    squidaioinfo_t *aioinfo = (squidaioinfo_t *) sd->fsdata;

    LOCAL_ARRAY(char, fullfilename, SQUID_MAXPATHLEN);
    assert(0 <= subdirn && subdirn < aioinfo->l1);
    snprintf(fullfilename, SQUID_MAXPATHLEN, "%s/%02X", sd->path, subdirn);
    return fullfilename;
}

static int
storeAufsDirCreateDirectory(const char *path, int should_exist)
{
    int created = 0;
    struct stat st;
    getCurrentTime();
    if (0 == stat(path, &st)) {
	if (S_ISDIR(st.st_mode)) {
	    debug(47, should_exist ? 3 : 1) ("%s exists\n", path);
	} else {
	    fatalf("Swap directory %s is not a directory.", path);
	}
#ifdef _SQUID_MSWIN_
    } else if (0 == mkdir(path)) {
#else
    } else if (0 == mkdir(path, 0755)) {
#endif
	debug(47, should_exist ? 1 : 3) ("%s created\n", path);
	created = 1;
    } else {
	fatalf("Failed to make swap directory %s: %s",
	    path, xstrerror());
    }
    return created;
}

static int
storeAufsDirVerifyDirectory(const char *path)
{
    struct stat sb;
    if (stat(path, &sb) < 0) {
	debug(47, 0) ("%s: %s\n", path, xstrerror());
	return -1;
    }
    if (S_ISDIR(sb.st_mode) == 0) {
	debug(47, 0) ("%s is not a directory\n", path);
	return -1;
    }
    return 0;
}

/*
 * This function is called by storeAufsDirInit().  If this returns < 0,
 * then Squid exits, complains about swap directories not
 * existing, and instructs the admin to run 'squid -z'
 */
static int
storeAufsDirVerifyCacheDirs(SwapDir * sd)
{
    squidaioinfo_t *aioinfo = (squidaioinfo_t *) sd->fsdata;
    int j;
    const char *path = sd->path;

    if (storeAufsDirVerifyDirectory(path) < 0)
	return -1;
    for (j = 0; j < aioinfo->l1; j++) {
	path = storeAufsDirSwapSubDir(sd, j);
	if (storeAufsDirVerifyDirectory(path) < 0)
	    return -1;
    }
    return 0;
}

static void
storeAufsDirCreateSwapSubDirs(SwapDir * sd)
{
    squidaioinfo_t *aioinfo = (squidaioinfo_t *) sd->fsdata;
    int i, k;
    int should_exist;
    LOCAL_ARRAY(char, name, MAXPATHLEN);
    for (i = 0; i < aioinfo->l1; i++) {
	snprintf(name, MAXPATHLEN, "%s/%02X", sd->path, i);
	if (storeAufsDirCreateDirectory(name, 0))
	    should_exist = 0;
	else
	    should_exist = 1;
	debug(47, 1) ("Making directories in %s\n", name);
	for (k = 0; k < aioinfo->l2; k++) {
	    snprintf(name, MAXPATHLEN, "%s/%02X/%02X", sd->path, i, k);
	    storeAufsDirCreateDirectory(name, should_exist);
	}
    }
}

static void
storeAufsCheckConfig(SwapDir * sd)
{
    if (!opt_create_swap_dirs)
	requirePathnameExists("cache_dir", sd->path);
}


/*!
 * @function
 *	storeAufsDirInit
 * @abstract
 *	Initialise the given configured AUFS storedir
 * @discussion
 *	This function completes the initial storedir setup, opens the swaplog
 *	file and begins the rebuild process.
 *
 *	It is quite possible that the swaplog will be appended to by incoming
 *	requests _WHILST_ also being read from during the rebuild process.
 *	This needs to be looked at and fixed.
 */
static void
storeAufsDirInit(SwapDir * sd)
{
    static int started_clean_event = 0;
    static const char *errmsg =
    "\tFailed to verify one of the swap directories, Check cache.log\n"
    "\tfor details.  Run 'squid -z' to create swap directories\n"
    "\tif needed, or if running Squid for the first time.";
    storeAufsDirInitBitmap(sd);
    if (storeAufsDirVerifyCacheDirs(sd) < 0)
	fatal(errmsg);

    /* Override the default number of threads if needed before squidaio_init() is called */
    if (Config.aiops.n_aiops_threads > -1)
	squidaio_nthreads = Config.aiops.n_aiops_threads;

    squidaio_init();
    storeAufsDirOpenSwapLog(sd);
    storeAufsDirRebuild(sd);
    if (!started_clean_event) {
	eventAdd("storeDirClean", storeAufsDirCleanEvent, NULL, 15.0, 1);
	started_clean_event = 1;
    }
    (void) storeDirGetBlkSize(sd->path, &sd->fs.blksize);
}

static void
storeAufsDirNewfs(SwapDir * sd)
{
    debug(47, 3) ("Creating swap space in %s\n", sd->path);
    storeAufsDirCreateDirectory(sd->path, 0);
    storeAufsDirCreateSwapSubDirs(sd);
}

static int
rev_int_sort(const void *A, const void *B)
{
    const int *i1 = A;
    const int *i2 = B;
    return *i2 - *i1;
}

static int
storeAufsDirClean(int swap_index)
{
    DIR *dp = NULL;
    struct dirent *de = NULL;
    LOCAL_ARRAY(char, p1, MAXPATHLEN + 1);
    LOCAL_ARRAY(char, p2, MAXPATHLEN + 1);
#if USE_TRUNCATE
    struct stat sb;
#endif
    int files[20];
    int swapfileno;
    int fn;			/* same as swapfileno, but with dirn bits set */
    int n = 0;
    int k = 0;
    int N0, N1, N2;
    int D0, D1, D2;
    SwapDir *SD;
    squidaioinfo_t *aioinfo;
    N0 = n_asyncufs_dirs;
    D0 = asyncufs_dir_index[swap_index % N0];
    SD = &Config.cacheSwap.swapDirs[D0];
    aioinfo = (squidaioinfo_t *) SD->fsdata;
    N1 = aioinfo->l1;
    D1 = (swap_index / N0) % N1;
    N2 = aioinfo->l2;
    D2 = ((swap_index / N0) / N1) % N2;
    snprintf(p1, SQUID_MAXPATHLEN, "%s/%02X/%02X",
	Config.cacheSwap.swapDirs[D0].path, D1, D2);
    debug(36, 3) ("storeDirClean: Cleaning directory %s\n", p1);
    dp = opendir(p1);
    if (dp == NULL) {
	if (errno == ENOENT) {
	    debug(36, 0) ("storeDirClean: WARNING: Creating %s\n", p1);
#ifdef _SQUID_MSWIN_
	    if (mkdir(p1) == 0)
#else
	    if (mkdir(p1, 0777) == 0)
#endif
		return 0;
	}
	debug(50, 0) ("storeDirClean: %s: %s\n", p1, xstrerror());
	safeunlink(p1, 1);
	return 0;
    }
    while ((de = readdir(dp)) != NULL && k < 20) {
	if (sscanf(de->d_name, "%X", &swapfileno) != 1)
	    continue;
	fn = swapfileno;	/* XXX should remove this cruft ! */
	if (storeAufsDirValidFileno(SD, fn, 1))
	    if (storeAufsDirMapBitTest(SD, fn))
		if (storeAufsFilenoBelongsHere(fn, D0, D1, D2))
		    continue;
#if USE_TRUNCATE
	if (!stat(de->d_name, &sb))
	    if (sb.st_size == 0)
		continue;
#endif
	files[k++] = swapfileno;
    }
    closedir(dp);
    if (k == 0)
	return 0;
    qsort(files, k, sizeof(int), rev_int_sort);
    if (k > 10)
	k = 10;
    for (n = 0; n < k; n++) {
	debug(36, 3) ("storeDirClean: Cleaning file %08X\n", files[n]);
	snprintf(p2, MAXPATHLEN + 1, "%s/%08X", p1, files[n]);
#if USE_TRUNCATE
	truncate(p2, 0);
#else
	safeunlink(p2, 0);
#endif
	statCounter.swap.files_cleaned++;
    }
    debug(36, 3) ("Cleaned %d unused files from %s\n", k, p1);
    return k;
}

static void
storeAufsDirCleanEvent(void *unused)
{
    static int swap_index = -1;
    int j = 0;
    int n = 0;
    /*
     * Assert that there are AUFS cache_dirs configured, otherwise
     * we should never be called.
     */
    if (swap_index == -1) {
	SwapDir *sd;
	squidaioinfo_t *aioinfo;
	/*
	 * Start the storeAufsDirClean() swap_index with a random
	 * value.  j equals the total number of AUFS level 2
	 * swap directories
	 */
	for (n = 0; n < n_asyncufs_dirs; n++) {
	    sd = &Config.cacheSwap.swapDirs[asyncufs_dir_index[n]];
	    aioinfo = (squidaioinfo_t *) sd->fsdata;
	    j += (aioinfo->l1 * aioinfo->l2);
	}
	swap_index = (int) (squid_random() % j);
    }
    if (0 == store_dirs_rebuilding) {
	n = storeAufsDirClean(swap_index);
	swap_index++;
	if (swap_index < 0)
	    swap_index = 0;
    }
    eventAdd("storeDirClean", storeAufsDirCleanEvent, NULL,
	15.0 * exp(-0.25 * n), 1);
}

/*
 * Does swapfile number 'fn' belong in cachedir #F0,
 * level1 dir #F1, level2 dir #F2?
 */
int
storeAufsFilenoBelongsHere(int fn, int F0, int F1, int F2)
{
    int D1, D2;
    int L1, L2;
    int filn = fn;
    squidaioinfo_t *aioinfo;
    assert(F0 < Config.cacheSwap.n_configured);
    aioinfo = (squidaioinfo_t *) Config.cacheSwap.swapDirs[F0].fsdata;
    L1 = aioinfo->l1;
    L2 = aioinfo->l2;
    D1 = ((filn / L2) / L2) % L1;
    if (F1 != D1)
	return 0;
    D2 = (filn / L2) % L2;
    if (F2 != D2)
	return 0;
    return 1;
}

int
storeAufsDirValidFileno(SwapDir * SD, sfileno filn, int flag)
{
    squidaioinfo_t *aioinfo = (squidaioinfo_t *) SD->fsdata;
    if (filn < 0)
	return 0;
    /*
     * If flag is set it means out-of-range file number should
     * be considered invalid.
     */
    if (flag)
	if (filn > aioinfo->map->max_n_files)
	    return 0;
    return 1;
}

void
storeAufsDirMaintain(SwapDir * SD)
{
    squidaioinfo_t *aioinfo = (squidaioinfo_t *) SD->fsdata;
    StoreEntry *e = NULL;
    int removed = 0;
    int max_scan;
    int max_remove;
    double f;
    RemovalPurgeWalker *walker;
    /* We can't delete objects while rebuilding swap */
    if (store_dirs_rebuilding) {
	return;
    } else {
	f = (double) (SD->cur_size - SD->low_size) / (SD->max_size - SD->low_size);
	f = f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f;
	max_scan = (int) (f * 400.0 + 100.0);
	max_remove = (int) (f * 70.0 + 10.0);
	/*
	 * This is kinda cheap, but so we need this priority hack?
	 */
    }
    debug(47, 3) ("storeMaintainSwapSpace: f=%f, max_scan=%d, max_remove=%d\n",
	f, max_scan, max_remove);
    walker = SD->repl->PurgeInit(SD->repl, max_scan);
    while (1) {
	if (SD->cur_size < SD->low_size && aioinfo->map->n_files_in_map < FILEMAP_MAX)
	    break;
	if (removed >= max_remove)
	    break;
	e = walker->Next(walker);
	if (!e)
	    break;		/* no more objects */
	removed++;
	storeRelease(e);
	if (aioQueueSize() > MAGIC2)
	    break;
    }
    walker->Done(walker);
    debug(47, (removed ? 2 : 3)) ("storeAufsDirMaintain: %s removed %d/%d f=%.03f max_scan=%d\n",
	SD->path, removed, max_remove, f, max_scan);
}

/*
 * storeAufsDirCheckObj
 *
 * This routine is called by storeDirSelectSwapDir to see if the given
 * object is able to be stored on this filesystem. AUFS filesystems will
 * happily store anything as long as the LRU time isn't too small.
 */
int
storeAufsDirCheckObj(SwapDir * SD, const StoreEntry * e)
{
    return 1;
}

int
storeAufsDirCheckLoadAv(SwapDir * SD, store_op_t op)
{
    int loadav, ql;

    ql = aioQueueSize();
    if (ql == 0) {
	return AUFS_LOAD_BASE;
    }
    loadav = AUFS_LOAD_BASE + (ql * AUFS_LOAD_QUEUE_WEIGHT / MAGIC1);
    return loadav;
}

/*
 * storeAufsDirRefObj
 *
 * This routine is called whenever an object is referenced, so we can
 * maintain replacement information within the storage fs.
 */
void
storeAufsDirRefObj(SwapDir * SD, StoreEntry * e)
{
    debug(47, 3) ("storeAufsDirRefObj: referencing %p %d/%d\n", e, e->swap_dirn,
	e->swap_filen);
    if (SD->repl->Referenced)
	SD->repl->Referenced(SD->repl, e, &e->repl);
}

/*
 * storeAufsDirUnrefObj
 * This routine is called whenever the last reference to an object is
 * removed, to maintain replacement information within the storage fs.
 */
void
storeAufsDirUnrefObj(SwapDir * SD, StoreEntry * e)
{
    debug(47, 3) ("storeAufsDirUnrefObj: referencing %p %d/%d\n", e, e->swap_dirn,
	e->swap_filen);
    if (SD->repl->Dereferenced)
	SD->repl->Dereferenced(SD->repl, e, &e->repl);
}

/*
 * storeAufsDirUnlinkFile
 *
 * This routine unlinks a file and pulls it out of the bitmap.
 * It used to be in storeAufsUnlink(), however an interface change
 * forced this bit of code here. Eeek.
 */
void
storeAufsDirUnlinkFile(SwapDir * SD, sfileno f)
{
    debug(79, 3) ("storeAufsDirUnlinkFile: unlinking fileno %08X\n", f);
    /* storeAufsDirMapBitReset(SD, f); */
#if USE_TRUNCATE
    aioTruncate(storeAufsDirFullPath(SD, f, NULL), 0, NULL, NULL);
#else
    aioUnlink(storeAufsDirFullPath(SD, f, NULL), NULL, NULL);
#endif
}

/*
 * Add and remove the given StoreEntry from the replacement policy in
 * use.
 */

void
storeAufsDirReplAdd(SwapDir * SD, StoreEntry * e)
{
    debug(47, 4) ("storeAufsDirReplAdd: added node %p to dir %d\n", e,
	SD->index);
    SD->repl->Add(SD->repl, e, &e->repl);
}


void
storeAufsDirReplRemove(StoreEntry * e)
{
    SwapDir *SD = INDEXSD(e->swap_dirn);
    debug(47, 4) ("storeAufsDirReplRemove: remove node %p from dir %d\n", e,
	SD->index);
    SD->repl->Remove(SD->repl, e, &e->repl);
}



/* ========== LOCAL FUNCTIONS ABOVE, GLOBAL FUNCTIONS BELOW ========== */

void
storeAufsDirStats(SwapDir * SD, StoreEntry * sentry)
{
    squidaioinfo_t *aioinfo = SD->fsdata;
#ifdef HAVE_STATVFS
    fsblkcnt_t totl_kb;
    fsblkcnt_t free_kb;
    fsfilcnt_t totl_in;
    fsfilcnt_t free_in;
#else
    int totl_kb = 0;
    int free_kb = 0;
    int totl_in = 0;
    int free_in = 0;
#endif
    int x;
    storeAppendPrintf(sentry, "First level subdirectories: %d\n", aioinfo->l1);
    storeAppendPrintf(sentry, "Second level subdirectories: %d\n", aioinfo->l2);
    storeAppendPrintf(sentry, "Maximum Size: %d KB\n", SD->max_size);
    storeAppendPrintf(sentry, "Current Size: %d KB\n", SD->cur_size);
    storeAppendPrintf(sentry, "Percent Used: %0.2f%%\n",
	100.0 * SD->cur_size / SD->max_size);
    storeAppendPrintf(sentry, "Current load metric: %d / %d\n", storeAufsDirCheckLoadAv(SD, ST_OP_CREATE), MAX_LOAD_VALUE);
    storeAppendPrintf(sentry, "Filemap bits in use: %d of %d (%d%%)\n",
	aioinfo->map->n_files_in_map, aioinfo->map->max_n_files,
	percent(aioinfo->map->n_files_in_map, aioinfo->map->max_n_files));
    x = storeDirGetUFSStats(SD->path, &totl_kb, &free_kb, &totl_in, &free_in);
    if (0 == x) {
#ifdef HAVE_STATVFS
	storeAppendPrintf(sentry, "Filesystem Space in use: %" PRIu64 "/%" PRIu64 " KB (%.0f%%)\n",
	    (uint64_t) (totl_kb - free_kb),
	    (uint64_t) totl_kb,
	    dpercent(totl_kb - free_kb, totl_kb));
	storeAppendPrintf(sentry, "Filesystem Inodes in use: %" PRIu64 "/%" PRIu64 " (%.0f%%)\n",
	    (uint64_t) (totl_in - free_in),
	    (uint64_t) totl_in,
	    dpercent(totl_in - free_in, totl_in));
#else
	storeAppendPrintf(sentry, "Filesystem Space in use: %d/%d KB (%d%%)\n",
	    totl_kb - free_kb,
	    totl_kb,
	    percent(totl_kb - free_kb, totl_kb));
	storeAppendPrintf(sentry, "Filesystem Inodes in use: %d/%d (%d%%)\n",
	    totl_in - free_in,
	    totl_in,
	    percent(totl_in - free_in, totl_in));
#endif
    }
    storeAppendPrintf(sentry, "Flags:");
    if (SD->flags.selected)
	storeAppendPrintf(sentry, " SELECTED");
    if (SD->flags.read_only)
	storeAppendPrintf(sentry, " READ-ONLY");
    storeAppendPrintf(sentry, "\n");
}

static struct cache_dir_option options[] =
{
#if NOT_YET_DONE
    {"L1", storeAufsDirParseL1, storeAufsDirDumpL1},
    {"L2", storeAufsDirParseL2, storeAufsDirDumpL2},
#endif
    {NULL, NULL}
};

/*
 * storeAufsDirReconfigure
 *
 * This routine is called when the given swapdir needs reconfiguring 
 */
static void
storeAufsDirReconfigure(SwapDir * sd, int index, char *path)
{
    int i;
    int size;
    int l1;
    int l2;

    i = GetInteger();
    size = i << 10;		/* Mbytes to kbytes */
    if (size <= 0)
	fatal("storeAufsDirReconfigure: invalid size value");
    i = GetInteger();
    l1 = i;
    if (l1 <= 0)
	fatal("storeAufsDirReconfigure: invalid level 1 directories value");
    i = GetInteger();
    l2 = i;
    if (l2 <= 0)
	fatal("storeAufsDirReconfigure: invalid level 2 directories value");

    /* just reconfigure it */
    if (size == sd->max_size)
	debug(3, 1) ("Cache dir '%s' size remains unchanged at %d KB\n",
	    path, size);
    else
	debug(3, 1) ("Cache dir '%s' size changed to %d KB\n",
	    path, size);
    sd->max_size = size;

    parse_cachedir_options(sd, options, 0);

    return;
}

void
storeAufsDirDump(StoreEntry * entry, SwapDir * s)
{
    squidaioinfo_t *aioinfo = (squidaioinfo_t *) s->fsdata;
    storeAppendPrintf(entry, " %d %d %d",
	s->max_size >> 10,
	aioinfo->l1,
	aioinfo->l2);
    dump_cachedir_options(entry, options, s);
}

/*
 * Only "free" the filesystem specific stuff here
 */
static void
storeAufsDirFree(SwapDir * s)
{
    squidaioinfo_t *aioinfo = (squidaioinfo_t *) s->fsdata;
    if (aioinfo->swaplog_fd > -1) {
	file_close(aioinfo->swaplog_fd);
	aioinfo->swaplog_fd = -1;
    }
    filemapFreeMemory(aioinfo->map);
    xfree(aioinfo);
    s->fsdata = NULL;		/* Will aid debugging... */
}

char *
storeAufsDirFullPath(SwapDir * SD, sfileno filn, char *fullpath)
{
    LOCAL_ARRAY(char, fullfilename, SQUID_MAXPATHLEN);
    squidaioinfo_t *aioinfo = (squidaioinfo_t *) SD->fsdata;
    int L1 = aioinfo->l1;
    int L2 = aioinfo->l2;
    if (!fullpath)
	fullpath = fullfilename;
    fullpath[0] = '\0';
    snprintf(fullpath, SQUID_MAXPATHLEN, "%s/%02X/%02X/%08X",
	SD->path,
	((filn / L2) / L2) % L1,
	(filn / L2) % L2,
	filn);
    return fullpath;
}

/*
 * storeAufsCleanupDoubleCheck
 *
 * This is called by storeCleanup() if -S was given on the command line.
 */
static int
storeAufsCleanupDoubleCheck(SwapDir * sd, StoreEntry * e)
{
    struct stat sb;
    if (stat(storeAufsDirFullPath(sd, e->swap_filen, NULL), &sb) < 0) {
	debug(47, 0) ("storeAufsCleanupDoubleCheck: MISSING SWAP FILE\n");
	debug(47, 0) ("storeAufsCleanupDoubleCheck: FILENO %08X\n", e->swap_filen);
	debug(47, 0) ("storeAufsCleanupDoubleCheck: PATH %s\n",
	    storeAufsDirFullPath(sd, e->swap_filen, NULL));
	storeEntryDump(e, 0);
	return -1;
    }
    if (e->swap_file_sz != sb.st_size) {
	debug(47, 0) ("storeAufsCleanupDoubleCheck: SIZE MISMATCH\n");
	debug(47, 0) ("storeAufsCleanupDoubleCheck: FILENO %08X\n", e->swap_filen);
	debug(47, 0) ("storeAufsCleanupDoubleCheck: PATH %s\n",
	    storeAufsDirFullPath(sd, e->swap_filen, NULL));
	debug(47, 0) ("storeAufsCleanupDoubleCheck: ENTRY SIZE: %ld, FILE SIZE: %ld\n",
	    (long int) e->swap_file_sz, (long int) sb.st_size);
	storeEntryDump(e, 0);
	return -1;
    }
    return 0;
}

static void
storeAufsSync(SwapDir * sd)
{
	aioSync();
}

/*
 * storeAufsDirParse *
 * Called when a *new* fs is being setup.
 */
static void
storeAufsDirParse(SwapDir * sd, int index, char *path)
{
    int i;
    int size;
    int l1;
    int l2;
    squidaioinfo_t *aioinfo;

    i = GetInteger();
    size = i << 10;		/* Mbytes to kbytes */
    if (size <= 0)
	fatal("storeAufsDirParse: invalid size value");
    i = GetInteger();
    l1 = i;
    if (l1 <= 0)
	fatal("storeAufsDirParse: invalid level 1 directories value");
    i = GetInteger();
    l2 = i;
    if (l2 <= 0)
	fatal("storeAufsDirParse: invalid level 2 directories value");

    aioinfo = xmalloc(sizeof(squidaioinfo_t));
    if (aioinfo == NULL)
	fatal("storeAufsDirParse: couldn't xmalloc() squidaioinfo_t!\n");

    sd->index = index;
    sd->path = xstrdup(path);
    sd->max_size = size;
    sd->fsdata = aioinfo;
    aioinfo->l1 = l1;
    aioinfo->l2 = l2;
    aioinfo->swaplog_fd = -1;
    aioinfo->map = NULL;	/* Debugging purposes */
    aioinfo->suggest = 0;
    sd->checkconfig = storeAufsCheckConfig;
    sd->init = storeAufsDirInit;
    sd->newfs = storeAufsDirNewfs;
    sd->dump = storeAufsDirDump;
    sd->freefs = storeAufsDirFree;
    sd->dblcheck = storeAufsCleanupDoubleCheck;
    sd->statfs = storeAufsDirStats;
    sd->maintainfs = storeAufsDirMaintain;
    sd->checkobj = storeAufsDirCheckObj;
    sd->checkload = storeAufsDirCheckLoadAv;
    sd->refobj = storeAufsDirRefObj;
    sd->unrefobj = storeAufsDirUnrefObj;
    sd->callback = NULL;
    sd->sync = storeAufsSync;
    sd->obj.create = storeAufsCreate;
    sd->obj.open = storeAufsOpen;
    sd->obj.close = storeAufsClose;
    sd->obj.read = storeAufsRead;
    sd->obj.write = storeAufsWrite;
    sd->obj.unlink = storeAufsUnlink;
    sd->obj.recycle = storeAufsRecycle;
    sd->log.open = storeAufsDirOpenSwapLog;
    sd->log.close = storeAufsDirCloseSwapLog;
    sd->log.write = storeAufsDirSwapLog;
    sd->log.clean.start = storeAufsDirWriteCleanStart;
    sd->log.clean.nextentry = storeAufsDirCleanLogNextEntry;
    sd->log.clean.done = storeAufsDirWriteCleanDone;

    parse_cachedir_options(sd, options, 0);

    /* Initialise replacement policy stuff */
    sd->repl = createRemovalPolicy(Config.replPolicy);

    asyncufs_dir_index = realloc(asyncufs_dir_index, (n_asyncufs_dirs + 1) * sizeof(*asyncufs_dir_index));
    asyncufs_dir_index[n_asyncufs_dirs++] = index;
    aiops_default_ndirs ++;
}

/*
 * Initial setup / end destruction
 */
static void
storeAufsDirDone(void)
{
    aioDone();
    memPoolDestroy(squidaio_state_pool);
    memPoolDestroy(aufs_qread_pool);
    memPoolDestroy(aufs_qwrite_pool);
    asyncufs_initialised = 0;
}

void
storeFsSetup_aufs(storefs_entry_t * storefs)
{
    assert(!asyncufs_initialised);
    storefs->parsefunc = storeAufsDirParse;
    storefs->reconfigurefunc = storeAufsDirReconfigure;
    storefs->donefunc = storeAufsDirDone;
    squidaio_state_pool = memPoolCreate("AUFS IO State data", sizeof(squidaiostate_t));
    aufs_qread_pool = memPoolCreate("AUFS Queued read data",
	sizeof(queued_read));
    aufs_qwrite_pool = memPoolCreate("AUFS Queued write data",
	sizeof(queued_write));

    asyncufs_initialised = 1;
    aioInit();
}
