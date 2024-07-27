
/*
 * $Id: cbdata.c 14762 2010-08-24 08:38:54Z adrian.chadd $
 *
 * DEBUG: section 45    Callback Data Registry
 * ORIGINAL AUTHOR: Duane Wessels
 * Modified by Moez Mahfoudh (08/12/2000)
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

/*
 * These routines manage a set of registered callback data pointers.
 * One of the easiest ways to make Squid coredump is to issue a 
 * callback to for some data structure which has previously been
 * freed.  With these routines, we register (add) callback data
 * pointers, lock them just before registering the callback function,
 * validate them before issuing the callback, and then free them
 * when finished.
 * 
 * In terms of time, the sequence goes something like this:
 * 
 * foo = cbdataAlloc(sizeof(foo),NULL);
 * ...
 * some_blocking_operation(..., callback_func, foo);
 *   cbdataLock(foo);
 *   ...
 *   some_blocking_operation_completes()
 *   if (cbdataValid(foo))
 *   callback_func(..., foo)
 *   cbdataUnlock(foo);
 * ...
 * cbdataFree(foo);
 * 
 * The nice thing is that, we do not need to require that Unlock
 * occurs before Free.  If the Free happens first, then the 
 * callback data is marked invalid and the callback will never
 * be made.  When we Unlock and the lock count reaches zero,
 * we free the memory if it is marked invalid.
 */

#include "squid.h"

static OBJH cbdataDump;

void
cbdataLocalInit(void)
{
    debugs(45, 3, "cbdataLocalInit");
    cachemgrRegister("cbdata",
	"Callback Data Registry Contents",
	cbdataDump, NULL, NULL, 0, 1, 0);
}

#if HASHED_CBDATA
static void
cbdataDump(StoreEntry * sentry, void* data)
{
    cbdata *n;

    hash_first(cbdata_htable);
    while ((n = (cbdata *) hash_next(cbdata_htable))) {
#if CBDATA_DEBUG
        storeAppendPrintf(sentry, "cbdata=%p	key=%p	type=%s	locks=%d	valid=%d	%s:%d\n",
	  n, n->hash.key, cbdata_index[n->type].pool->label, n->locks, n->valid, n->file, n->line);
#else
        storeAppendPrintf(sentry, "cbdata=%p	key=%p	type=%d	locks=%d	valid=%d\n",
	  n, n->hash.key, cbdata_index[n->type].pool->label, n->locks, n->valid);
#endif
    }

    storeAppendPrintf(sentry, "%d cbdata entries\n", cbdataCount);
}
#else
static void
cbdataDump(StoreEntry * sentry, void* data)
{
    storeAppendPrintf(sentry, "%d cbdata entries\n", cbdataCount);
    storeAppendPrintf(sentry, "see also memory pools section\n");
}
#endif
