
/*
 * $Id: store_bitmap_aufs.c 14618 2010-04-18 14:02:18Z adrian.chadd $
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

#include "store_asyncufs.h"
#include "store_rebuild_aufs.h"

/*
 * These functions were ripped straight out of the heart of store_dir.c.
 * They assume that the given filenum is on a asyncufs partiton, which may or
 * may not be true.. 
 * XXX this evilness should be tidied up at a later date!
 */

int
storeAufsDirMapBitTest(SwapDir * SD, sfileno filn)
{
    squidaioinfo_t *aioinfo;
    aioinfo = (squidaioinfo_t *) SD->fsdata;
    return file_map_bit_test(aioinfo->map, filn);
}

void
storeAufsDirMapBitSet(SwapDir * SD, sfileno filn)
{
    squidaioinfo_t *aioinfo;
    aioinfo = (squidaioinfo_t *) SD->fsdata;
    file_map_bit_set(aioinfo->map, filn);
}

void
storeAufsDirMapBitReset(SwapDir * SD, sfileno filn)
{
    squidaioinfo_t *aioinfo;
    aioinfo = (squidaioinfo_t *) SD->fsdata;
    /*
     * We have to test the bit before calling file_map_bit_reset.
     * file_map_bit_reset doesn't do bounds checking.  It assumes
     * filn is a valid file number, but it might not be because
     * the map is dynamic in size.  Also clearing an already clear
     * bit puts the map counter of-of-whack.
     */
    if (file_map_bit_test(aioinfo->map, filn))
	file_map_bit_reset(aioinfo->map, filn);
}

int
storeAufsDirMapBitAllocate(SwapDir * SD)
{
    squidaioinfo_t *aioinfo = (squidaioinfo_t *) SD->fsdata;
    int fn;
    fn = file_map_allocate(aioinfo->map, aioinfo->suggest);
    file_map_bit_set(aioinfo->map, fn);
    aioinfo->suggest = fn + 1;
    return fn;
}

/*
 * Initialise the asyncufs bitmap
 *
 * If there already is a bitmap, and the numobjects is larger than currently
 * configured, we allocate a new bitmap and 'grow' the old one into it.
 */
void
storeAufsDirInitBitmap(SwapDir * sd)
{
    squidaioinfo_t *aioinfo = (squidaioinfo_t *) sd->fsdata;

    if (aioinfo->map == NULL) {
	/* First time */
	aioinfo->map = file_map_create();
    } else if (aioinfo->map->max_n_files) {
	/* it grew, need to expand */
	/* XXX We don't need it anymore .. */
    }
    /* else it shrunk, and we leave the old one in place */
}
