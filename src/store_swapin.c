
/*
 * $Id: store_swapin.c 12288 2007-12-23 11:32:11Z adrian $
 *
 * DEBUG: section 20    Storage Manager Swapin Functions
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

static STIOCB storeSwapInFileClosed;
static STFNCB storeSwapInFileNotify;

void
storeSwapInStart(store_client * sc)
{
    StoreEntry *e = sc->entry;
    assert(e->mem_status == NOT_IN_MEMORY);
    if (!EBIT_TEST(e->flags, ENTRY_VALIDATED)) {
	/* We're still reloading and haven't validated this entry yet */
	return;
    }
    debugs(20, 3, "storeSwapInStart: called for %d %08X %s ",
	e->swap_dirn, e->swap_filen, storeKeyText(e->hash.key));
    if (e->swap_status != SWAPOUT_WRITING && e->swap_status != SWAPOUT_DONE) {
	debugs(20, 1, "storeSwapInStart: bad swap_status (%s)",
	    swapStatusStr[e->swap_status]);
	return;
    }
    if (e->swap_filen < 0) {
	debugs(20, 1, "storeSwapInStart: swap_filen < 0");
	return;
    }
    assert(e->mem_obj != NULL);
    debugs(20, 3, "storeSwapInStart: Opening fileno %08X",
	e->swap_filen);
    sc->swapin_sio = storeOpen(e, storeSwapInFileNotify, storeSwapInFileClosed,
	sc);
    cbdataLock(sc->swapin_sio);
}

static void
storeSwapInFileClosed(void *data, int errflag, storeIOState * sio)
{
    STNCB *callback;
    store_client *sc = data;
    debugs(20, 3, "storeSwapInFileClosed: sio=%p, errflag=%d",
	sio, errflag);
    cbdataUnlock(sio);
    sc->swapin_sio = NULL;
    if (errflag < 0)
	storeRelease(sc->entry);
    if ((callback = sc->new_callback)) {
	/* XXX [ahc] why doesn't this call storeClientCallback() ? */
	void *cbdata = sc->callback_data;
	mem_node_ref nr = sc->node_ref;
	nr = sc->node_ref;	/* XXX this should be a reference; and we should dereference our copy! */
	/* This code "transfers" its ownership (and reference) of the node_ref to the caller. Ugly, but works. */
	sc->node_ref.node = NULL;
	sc->node_ref.offset = -1;
	assert(errflag <= 0);
	sc->new_callback = NULL;
	sc->callback_data = NULL;
	if (cbdataValid(cbdata))
	    callback(cbdata, nr, errflag);
	cbdataUnlock(cbdata);
    }
    statCounter.swap.ins++;
}

static void
storeSwapInFileNotify(void *data, int errflag, storeIOState * sio)
{
    store_client *sc = data;
    StoreEntry *e = sc->entry;

    debugs(1, 3, "storeSwapInFileNotify: changing %d/%d to %d/%d", e->swap_filen, e->swap_dirn, sio->swap_filen, sio->swap_dirn);

    e->swap_filen = sio->swap_filen;
    e->swap_dirn = sio->swap_dirn;
}
