
/*
 * $Id: store_rebuild_coss.c 14698 2010-05-25 08:09:42Z swilton@q-net.net.au $
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
#include "../../libsqstore/store_log.h"

#include "store_coss.h"
#include "store_rebuild_coss.h"

static void
storeCossRebuildComplete(void *data)
{
    RebuildState *rb = data;
    SwapDir *SD = rb->sd;
    CossInfo *cs = SD->fsdata;
    store_dirs_rebuilding--;
    storeRebuildComplete(&rb->counts);
    debugs(47, 1, "COSS: %s: Rebuild Completed", stripePath(SD));
    cs->rebuild.rebuilding = 0;
    if (rb->helper.pid != -1)
        ipcClose(rb->helper.pid, rb->helper.r_fd, rb->helper.w_fd);
    safe_free(rb->rbuf.buf);
    debugs(47, 1, "  %d objects scanned, %d objects relocated, %d objects fresher, %d objects ignored",
        rb->counts.scancount, rb->cosscounts.reloc, rb->cosscounts.fresher, rb->cosscounts.unknown);
    if (rb->recent.timestamp > -1 && rb->recent.swap_filen > -1) {
	cs->curstripe = storeCossFilenoToStripe(cs, rb->recent.swap_filen);
        debugs(47, 1, "  Current stripe set to %d", cs->curstripe);
    }
    storeCossStartMembuf(SD);
    cbdataFree(rb);
}

static void
storeCoss_updateRecent(RebuildState *rb, storeSwapLogData *d)
{
	if (d->timestamp < rb->recent.timestamp)
		return;
	rb->recent.timestamp = d->timestamp;
	rb->recent.swap_filen = d->swap_filen;
}

static void
storeCoss_AddStoreEntry(RebuildState * rb, const cache_key * key, storeSwapLogData *d)
{
    StoreEntry *ne;
    SwapDir *SD = rb->sd;
    CossInfo *cs = SD->fsdata;
    rb->counts.objcount++;

    ne = new_StoreEntry(STORE_ENTRY_WITHOUT_MEMOBJ, NULL);
    ne->store_status = STORE_OK;
    storeSetMemStatus(ne, NOT_IN_MEMORY);
    ne->swap_status = SWAPOUT_DONE;
    ne->swap_filen = d->swap_filen;
    ne->swap_dirn = SD->index;
    ne->swap_file_sz = d->swap_file_sz;
    ne->lock_count = 0;
    ne->lastref = d->lastref;
    ne->timestamp = d->timestamp;
    ne->expires = d->expires;
    ne->lastmod = d->lastmod;
    ne->refcount = d->refcount;
    ne->flags = d->flags;
    EBIT_SET(ne->flags, ENTRY_CACHABLE);
    EBIT_CLR(ne->flags, RELEASE_REQUEST);
    EBIT_CLR(ne->flags, KEY_PRIVATE);
    ne->ping_status = PING_NONE;
    EBIT_CLR(ne->flags, ENTRY_VALIDATED);
    storeHashInsert(ne, key);	/* do it after we clear KEY_PRIVATE */
    storeCossAdd(SD, ne,  storeCossFilenoToStripe(cs, d->swap_filen));
    storeEntryDump(ne, 5);
    assert(ne->repl.data != NULL);
}

static void
storeCoss_DeleteStoreEntry(RebuildState * rb, const cache_key * key, StoreEntry * e)
{
    storeRecycle(e);
}

/*
 * Consider inserting the given StoreEntry into the given
 * COSS directory.
 *
 * The rules for doing this is reasonably simple:
 *
 * If the object doesn't exist in the cache then we simply
 * add it to the current stripe list
 *
 * If the object does exist in the cache then we compare
 * "freshness"; if the newer object is fresher then we
 * remove it from its stripe and re-add it to the current
 * stripe.
 */
static void
storeCoss_ConsiderStoreEntry(RebuildState * rb, const cache_key * key, storeSwapLogData * d)
{
    SwapDir *sd = rb->sd;
    StoreEntry *oe;

    /* Is it Private? Don't bother */
    if (EBIT_TEST(d->flags, KEY_PRIVATE)) {
        debugs(47, 3, "COSS: %s: filen %x private key flag set, ignoring.", stripePath(sd), d->swap_filen);
        rb->counts.badflags++;
        return;
    }

    /* Check for clashes */
    oe = storeGet(key);
    if (oe == NULL) {
	rb->cosscounts.new++;
	debugs(47, 3, "COSS: Adding filen %d", d->swap_filen);
	/* no clash! woo, can add and forget */
	storeCoss_updateRecent(rb, d);
	storeCoss_AddStoreEntry(rb, key, d);
	return;
    }
    /* This isn't valid - its possible we have a fresher object in another store */
    /* unlike the UFS-based stores we don't "delete" the disk object when we
     * have deleted the object; its one of the annoying things about COSS. */
    //assert(oe->swap_dirn == SD->index);
    /* Dang, its a clash. See if its fresher */

    /* Fresher? Its a new object: deallocate the old one, reallocate the new one */
    if (d->lastref > oe->lastref) {
	debugs(47, 3, "COSS: fresher object for filen %d found (%ld -> %ld)", oe->swap_filen, (long int) oe->timestamp, (long int) d->timestamp);
	rb->cosscounts.fresher++;
	storeCoss_DeleteStoreEntry(rb, key, oe);
	oe = NULL;
	storeCoss_updateRecent(rb, d);
	storeCoss_AddStoreEntry(rb, key, d);
	return;
    }
    /*
     * Not fresher? Its the same object then we /should/ probably relocate it; I'm
     * not sure what should be done here.
     */
    if (oe->timestamp == d->timestamp && oe->expires == d->expires) {
	debugs(47, 3, "COSS: filen %d -> %d (since they're the same!)", oe->swap_filen, d->swap_filen);
	rb->cosscounts.reloc++;
	storeCoss_DeleteStoreEntry(rb, key, oe);
	oe = NULL;
	storeCoss_updateRecent(rb, d);
	storeCoss_AddStoreEntry(rb, key, d);
	return;
    }
    debugs(47, 3, "COSS: filen %d: ignoring this one for some reason", d->swap_filen);
    rb->cosscounts.unknown++;
}

/*
 * This is a cut-and-paste from the AUFS helper read function.
 * Tsk!
 */
static void
storeCossRebuildHelperRead(int fd, void *data)
{
        RebuildState *rb = data;
        SwapDir *sd = rb->sd;
        /* squidaioinfo_t *aioinfo = (squidaioinfo_t *) sd->fsdata; */
        int r, i;
        storeSwapLogData s;
	int t, p;

        assert(fd == rb->helper.r_fd);
        debugs(47, 5, "storeCossRebuildHelperRead: %s: ready for helper read", sd->path);

        assert(rb->rbuf.size - rb->rbuf.used > 0);
        debugs(47, 8, "storeCossRebuildHelperRead: %s: trying to read %d bytes", sd->path, rb->rbuf.size - rb->rbuf.used);
        r = FD_READ_METHOD(fd, rb->rbuf.buf + rb->rbuf.used, rb->rbuf.size - rb->rbuf.used);
        debugs(47, 8, "storeCossRebuildHelperRead: %s: read %d bytes", sd->path, r);
        if (r <= 0) {
                /* Error or EOF */
                debugs(47, 1, "storeCossRebuildHelperRead: %s: read returned %d; error/eof?", sd->path, r);
                storeCossRebuildComplete(rb);
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
				p = ((storeSwapLogProgress *)(&s))->progress;
				debugs(47, 3, "storeCOSSRebuildHelperRead: %s: SWAP_LOG_PROGRESS: total %d objects, progress %d objects", sd->path, t, p);
                                storeRebuildProgress(rb->sd->index, t, p);
                                break;
                        case SWAP_LOG_COMPLETED:
                                debugs(47, 1, "  %s: completed rebuild", sd->path);
                                storeCossRebuildComplete(rb);
                                return;
                        default:
                                rb->n_read++;
                                /* storeAufsDirRebuildFromSwapLogObject(rb, s); */
				storeCoss_ConsiderStoreEntry(rb, s.key, &s);
                                rb->counts.scancount++;
                }
                i += sizeof(storeSwapLogData);
        }
        debugs(47, 5, "storeCossRebuildHelperRead: %s: read %d entries", sd->path, (int) i / (int) sizeof(storeSwapLogData));

        /* Shuffle what is left to the beginning of the buffer */
        if (i < rb->rbuf.used) {
                memmove(rb->rbuf.buf, rb->rbuf.buf + i, rb->rbuf.used - i);
                rb->rbuf.used -= i;
        }

        /* Re-register */
        commSetSelect(rb->helper.r_fd, COMM_SELECT_READ, storeCossRebuildHelperRead, rb, 0);
}

static void
storeDirCoss_StartDiskRebuild(RebuildState * rb)
{
    SwapDir *SD = rb->sd;
    CossInfo *cs = SD->fsdata;
    const char * args[8];
    char num_stripes[128], block_size[128], stripe_size[128];

    /* Open the rebuild helper */
    snprintf(num_stripes, sizeof(num_stripes) - 1, "%d", cs->numstripes);
    snprintf(block_size, sizeof(block_size) - 1, "%d", 1 << cs->blksz_bits);
    snprintf(stripe_size, sizeof(stripe_size) - 1, "%d", COSS_MEMBUF_SZ);

    args[0] = Config.Program.coss_log_build;
    args[1] = "rebuild";
    args[2] = stripePath(SD);
    args[3] = block_size;
    args[4] = stripe_size;
    args[5] = num_stripes;
    args[6] = NULL;

    debugs(47, 2, "COSS: %s: Beginning disk rebuild.", stripePath(SD));

    rb->helper.pid = ipcCreate(IPC_STREAM, Config.Program.coss_log_build, args, "coss_rebuild helper",
      0, &rb->helper.r_fd, &rb->helper.w_fd, NULL);
    assert(rb->helper.pid != -1);

    /* Setup incoming read buffer */
    /* XXX eww, this should really be in a producer/consumer library damnit */
    rb->rbuf.buf = xmalloc(65536);
    rb->rbuf.size = 65536;
    rb->rbuf.used = 0;

    /* Register for read interest */
    commSetSelect(rb->helper.r_fd, COMM_SELECT_READ, storeCossRebuildHelperRead, rb, 0);

    assert(cs->rebuild.rebuilding == 0);
    assert(cs->numstripes > 0);
    assert(cs->fd >= 0);

    cs->rebuild.rebuilding = 1;

}

CBDATA_TYPE(RebuildState);
void
storeCossDirRebuild(SwapDir * sd)
{
    RebuildState *rb;
    int clean = 0;
    CBDATA_INIT_TYPE(RebuildState);
    rb = cbdataAlloc(RebuildState);
    rb->sd = sd;
    rb->flags.clean = (unsigned int) clean;
    rb->recent.swap_filen = -1;
    rb->recent.timestamp = -1;
    debugs(20, 1, "Rebuilding COSS storage in %s (DIRTY)", stripePath(sd));
    store_dirs_rebuilding++;
    storeDirCoss_StartDiskRebuild(rb);
}
