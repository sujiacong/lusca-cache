
/*
 * $Id$
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
#include "../../libsqstore/filemap.h"

#include "../../libasyncio/aiops.h"
#include "../../libasyncio/async_io.h"
#include "store_asyncufs.h"
#include "store_bitmap_aufs.h"
#include "store_rebuild_aufs.h"
#include "store_log_aufs.h"

#define STORE_META_BUFSZ 4096

/*
 * The AUFS rebuild process can take one of two main paths - either by logfile
 * or by directory.
 *
 * The logfile rebuild opens the swaplog and a clean swaplog, reads in
 * entries and writes out sanitised entries to the clean swaplog.
 * The clean swaplog is then moved into place over the original swaplog.
 *
 * The directory rebuild opens a "temporary" swaplog and writes out entries
 * to the temporary swaplog as the directory is walked. The temporary
 * swaplog is then moved into place over the original swaplog.
 *
 * Any objects which are to be removed for whatever reason (fresher objects
 * are available, they've expired, etc) are expired via storeRelease().
 * Their deletion will occur once all the stores have rebuilt rather than
 * the deletion taking place during the rebuild.
 */

static void
storeAufsDirRebuildComplete(RebuildState * rb)
{
    if (rb->log_fd) {
	debugs(47, 1, "Done reading %s swaplog (%d entries)",
	    rb->sd->path, rb->n_read);
	file_close(rb->log_fd);
	rb->log_fd = -1;
    } else {
	debugs(47, 1, "Done scanning %s (%d entries)",
	    rb->sd->path, rb->counts.scancount);
    }
    store_dirs_rebuilding--;
    storeAufsDirCloseTmpSwapLog(rb->sd);
    storeRebuildComplete(&rb->counts);
    if (rb->helper.pid != -1)
	ipcClose(rb->helper.pid, rb->helper.r_fd, rb->helper.w_fd);
    safe_free(rb->rbuf.buf);
    cbdataFree(rb);
}

/* Add a new object to the cache with empty memory copy and pointer to disk
 * use to rebuild store from disk. */
static StoreEntry *
storeAufsDirAddDiskRestore(SwapDir * SD, const cache_key * key,
    sfileno file_number,
    squid_file_sz swap_file_sz,
    time_t expires,
    time_t timestamp,
    time_t lastref,
    time_t lastmod,
    u_num32 refcount,
    u_short flags,
    int clean)
{
    StoreEntry *e = NULL;
    debugs(47, 5, "storeAufsAddDiskRestore: %s, fileno=%08X", storeKeyText(key), file_number);
    /* if you call this you'd better be sure file_number is not 
     * already in use! */
    e = new_StoreEntry(STORE_ENTRY_WITHOUT_MEMOBJ, NULL);
    e->store_status = STORE_OK;
    storeSetMemStatus(e, NOT_IN_MEMORY);
    e->swap_status = SWAPOUT_DONE;
    e->swap_filen = file_number;
    e->swap_dirn = SD->index;
    e->swap_file_sz = swap_file_sz;
    e->lock_count = 0;
    e->lastref = lastref;
    e->timestamp = timestamp;
    e->expires = expires;
    e->lastmod = lastmod;
    e->refcount = refcount;
    e->flags = flags;
    EBIT_SET(e->flags, ENTRY_CACHABLE);
    EBIT_CLR(e->flags, RELEASE_REQUEST);
    EBIT_CLR(e->flags, KEY_PRIVATE);
    e->ping_status = PING_NONE;
    EBIT_CLR(e->flags, ENTRY_VALIDATED);
    storeAufsDirMapBitSet(SD, e->swap_filen);
    storeHashInsert(e, key);	/* do it after we clear KEY_PRIVATE */
    storeAufsDirReplAdd(SD, e);
    return e;
}

static int
storeAufsDirRebuildFromSwapLogObject(RebuildState *rb, storeSwapLogData s)
{
	SwapDir *SD = rb->sd;
	StoreEntry *e = NULL;
	double x;
	int used;			/* is swapfile already in use? */
	int disk_entry_newer;	/* is the log entry newer than current entry? */

	/*
	 * BC: during 2.4 development, we changed the way swap file
	 * numbers are assigned and stored.  The high 16 bits used
	 * to encode the SD index number.  There used to be a call
	 * to storeDirProperFileno here that re-assigned the index 
	 * bits.  Now, for backwards compatibility, we just need
	 * to mask it off.
	 */
	s.swap_filen &= 0x00FFFFFF;
	debugs(47, 3, "storeAufsDirRebuildFromSwapLog: %s %s %08X",
	    swap_log_op_str[(int) s.op],
	    storeKeyText(s.key),
	    s.swap_filen);
	if (s.op == SWAP_LOG_ADD) {
	    (void) 0;
	} else if (s.op == SWAP_LOG_DEL) {
	    /* Delete unless we already have a newer copy */
	    if ((e = storeGet(s.key)) != NULL && s.lastref >= e->lastref) {
		/*
		 * Make sure we don't unlink the file, it might be
		 * in use by a subsequent entry.  Also note that
		 * we don't have to subtract from store_swap_size
		 * because adding to store_swap_size happens in
		 * the cleanup procedure.
		 */
		storeRecycle(e);
		rb->counts.cancelcount++;
	    }
	    return -1;
	} else {
	    x = log(++rb->counts.bad_log_op) / log(10.0);
	    if (0.0 == x - (double) (int) x)
		debugs(47, 1, "WARNING: %d invalid swap log entries found",
		    rb->counts.bad_log_op);
	    rb->counts.invalid++;
	    return -1;
	}
	if (!storeAufsDirValidFileno(SD, s.swap_filen, 0)) {
	    rb->counts.invalid++;
	    return -1;
	}
	if (EBIT_TEST(s.flags, KEY_PRIVATE)) {
	    rb->counts.badflags++;
	    return -1;
	}
	e = storeGet(s.key);
	used = storeAufsDirMapBitTest(SD, s.swap_filen);
	/* If this URL already exists in the cache, does the swap log
	 * appear to have a newer entry?  Compare 'lastref' from the
	 * swap log to e->lastref. */
	disk_entry_newer = e ? (s.lastref > e->lastref ? 1 : 0) : 0;
	if (used && !disk_entry_newer) {
	    /* log entry is old, ignore it */
	    rb->counts.clashcount++;
	    return -1;
	} else if (used && e && e->swap_filen == s.swap_filen && e->swap_dirn == SD->index) {
	    /* swapfile taken, same URL, newer, update meta */
	    if (e->store_status == STORE_OK) {
		e->lastref = s.timestamp;
		e->timestamp = s.timestamp;
		e->expires = s.expires;
		e->lastmod = s.lastmod;
		e->flags = s.flags;
		e->refcount += s.refcount;
		storeAufsDirUnrefObj(SD, e);
	    } else {
		debug_trap("storeAufsDirRebuildFromSwapLog: bad condition");
		debugs(47, 1, "\tSee %s:%d", __FILE__, __LINE__);
	    }
	    return -1;
	} else if (used) {
	    /* swapfile in use, not by this URL, log entry is newer */
	    /* This is sorta bad: the log entry should NOT be newer at this
	     * point.  If the log is dirty, the filesize check should have
	     * caught this.  If the log is clean, there should never be a
	     * newer entry. */
	    debugs(47, 1, "WARNING: newer swaplog entry for dirno %d, fileno %08X",
		SD->index, s.swap_filen);
	    /* I'm tempted to remove the swapfile here just to be safe,
	     * but there is a bad race condition in the NOVM version if
	     * the swapfile has recently been opened for writing, but
	     * not yet opened for reading.  Because we can't map
	     * swapfiles back to StoreEntrys, we don't know the state
	     * of the entry using that file.  */
	    /* We'll assume the existing entry is valid, probably because
	     * the swap file number got taken while we rebuild */
	    rb->counts.clashcount++;
	    return -1;
	} else if (e && !disk_entry_newer) {
	    /* key already exists, current entry is newer */
	    /* keep old, ignore new */
	    rb->counts.dupcount++;
	    return -1;
	} else if (e) {
	    /* key already exists, this swapfile not being used */
	    /* junk old, load new */
	    storeRecycle(e);
	    rb->counts.dupcount++;
	} else {
	    /* URL doesnt exist, swapfile not in use */
	    /* load new */
	    (void) 0;
	}
	/* update store_swap_size */
	rb->counts.objcount++;
	e = storeAufsDirAddDiskRestore(SD, s.key,
	    s.swap_filen,
	    s.swap_file_sz,
	    s.expires,
	    s.timestamp,
	    s.lastref,
	    s.lastmod,
	    s.refcount,
	    s.flags,
	    (int) rb->flags.clean);
	storeDirSwapLog(e, SWAP_LOG_ADD);
	return 1;
}

static void
storeAufsRebuildHelperRead(int fd, void *data)
{
	RebuildState *rb = data;
	SwapDir *sd = rb->sd;
	/* squidaioinfo_t *aioinfo = (squidaioinfo_t *) sd->fsdata; */
	int r, i;
	storeSwapLogData s;
	int t, p;

	assert(fd == rb->helper.r_fd);
	debugs(47, 5, "storeAufsRebuildHelperRead: %s: ready for helper read", sd->path);

	assert(rb->rbuf.size - rb->rbuf.used > 0);
	debugs(47, 8, "storeAufsRebuildHelperRead: %s: trying to read %d bytes", sd->path, rb->rbuf.size - rb->rbuf.used);
	r = FD_READ_METHOD(fd, rb->rbuf.buf + rb->rbuf.used, rb->rbuf.size - rb->rbuf.used);
	debugs(47, 8, "storeAufsRebuildHelperRead: %s: read %d bytes", sd->path, r);
	if (r <= 0) {
		/* Error or EOF */
		debugs(47, 1, "storeAufsRebuildHelperRead: %s: read returned %d; error/eof?", sd->path, r);
		if(!shutting_down)
		storeAufsDirRebuildComplete(rb);
		return;
	}
	rb->rbuf.used += r;

	/* We have some data; process what we can */
	i = 0;
	while (i + sizeof(storeSwapLogData) <= rb->rbuf.used) {
		memcpy(&s, rb->rbuf.buf + i, sizeof(storeSwapLogData));
		switch (s.op) {
			case SWAP_LOG_VERSION:
				break;
			case SWAP_LOG_PROGRESS:
				t = ((storeSwapLogProgress *)(&s))->total;
				p =  ((storeSwapLogProgress *)(&s))->progress;
				debugs(47, 3, "storeAufsRebuildHelperRead: %s: SWAP_LOG_PROGRESS: total %d objects, progress %d objects", sd->path, t, p);
				storeRebuildProgress(rb->sd->index, t, p);
				break;
			case SWAP_LOG_COMPLETED:
				debugs(47, 1, "  %s: completed rebuild", sd->path);
				storeAufsDirRebuildComplete(rb);
				return;
			default:
				rb->n_read++;
				storeAufsDirRebuildFromSwapLogObject(rb, s);
				rb->counts.scancount++;
		}
		i += sizeof(storeSwapLogData);
	}
	debugs(47, 5, "storeAufsRebuildHelperRead: %s: read %d entries", sd->path, i / (int) sizeof(storeSwapLogData));

	/* Shuffle what is left to the beginning of the buffer */
	if (i < rb->rbuf.used) {
		memmove(rb->rbuf.buf, rb->rbuf.buf + i, rb->rbuf.used - i);
		rb->rbuf.used -= i;
	}

	/* Re-register */
	commSetSelect(rb->helper.r_fd, COMM_SELECT_READ, storeAufsRebuildHelperRead, rb, 0);
}

CBDATA_TYPE(RebuildState);

/*!
 * @function
 *	storeAufsDirRebuild
 * @abstract
 *	Begin the directory rebuild process for the given AUFS directory
 * @discussion
 *	This function initialises the required bits for the AUFS directory
 *	rebuild, determines whether the rebuild should occur from the
 *	logfile or directory; and begins said process.
 *
 * @param	sd		SwapDir to begin the rebuild process
 */
void
storeAufsDirRebuild(SwapDir * sd)
{
    RebuildState *rb;
    int clean = 0;
    int zero = 0;
    int log_fd;
    CBDATA_INIT_TYPE(RebuildState);
    rb = cbdataAlloc(RebuildState);
    rb->sd = sd;
    const char * args[8];
    char l1[128], l2[128];
    squidaioinfo_t *aioinfo = (squidaioinfo_t *) sd->fsdata;

    /* Open the rebuild helper */
    snprintf(l1, sizeof(l1)-1, "%d", aioinfo->l1);
    snprintf(l2, sizeof(l2)-1, "%d", aioinfo->l2);
    args[0] = Config.Program.ufs_log_build;
    args[1] = "rebuild";
    args[2] = sd->path;
    args[3] = l1;
    args[4] = l2;
    args[5] = xstrdup(storeAufsDirSwapLogFile(sd, NULL));
    args[6] = NULL;

    rb->helper.pid = ipcCreate(IPC_STREAM, Config.Program.ufs_log_build, args, "ufs_rebuild helper",
      0, &rb->helper.r_fd, &rb->helper.w_fd, NULL);
    assert(rb->helper.pid != -1);
    safe_free(args[5]);

    /* Setup incoming read buffer */
    /* XXX eww, this should really be in a producer/consumer library damnit */
    rb->rbuf.buf = xmalloc(65536);
    rb->rbuf.size = 65536;
    rb->rbuf.used = 0;

    /* Register for read interest */
    commSetSelect(rb->helper.r_fd, COMM_SELECT_READ, storeAufsRebuildHelperRead, rb, 0);

    /* aaand we begin */
    log_fd = storeAufsDirOpenTmpSwapLog(sd, &clean, &zero);
    file_close(log_fd);	/* We don't need this open anyway..? */
    debugs(47, 1, "Rebuilding storage in %s (%s)", sd->path, clean ? "CLEAN" : "DIRTY");
    store_dirs_rebuilding++;
}
