
/*
 * $Id: cbdata.c 12314 2007-12-28 11:51:48Z hno $
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

#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>

#include "../libcore/valgrind.h"
#if WITH_VALGRIND
#define HASHED_CBDATA 1
#endif

#if HASHED_CBDATA
#include "../include/hash.h"
#endif

#include "../include/util.h"
#include "../include/Array.h"
#include "../include/Stack.h"
#include "../libcore/varargs.h"
#include "../libcore/gb.h"
#include "../libcore/dlink.h"
#include "../libcore/tools.h"
#include "../libcore/debug.h"
#include "../libmem/MemPool.h"
#include "../libmem/MemBufs.h"

#include "cbdata.h"


int cbdataCount = 0;

#if HASHED_CBDATA
static MemPool *cbdata_pool = NULL;
hash_table *cbdata_htable = NULL;
static HASHCMP cbdata_cmp;
static HASHHASH cbdata_hash;
#endif

struct cbdata_index_struct *cbdata_index = NULL;
int cbdata_types = 0;

#if HASHED_CBDATA
static int
cbdata_cmp(const void *p1, const void *p2)
{
    return (char *) p1 - (char *) p2;
}

static unsigned int
cbdata_hash(const void *p, unsigned int mod)
{
    return ((unsigned long) p >> 8) % mod;
}

#else
#define OFFSET_OF(type, member) ((size_t)(char *)&((type *)0L)->member)
#endif

void
cbdataInitType(cbdata_type type, const char *name, int size, FREE * free_func)
{
    char *label;
    if (type >= cbdata_types) {
	cbdata_index = xrealloc(cbdata_index, (type + 1) * sizeof(*cbdata_index));
	memset(&cbdata_index[cbdata_types], 0,
	    (type + 1 - cbdata_types) * sizeof(*cbdata_index));
	cbdata_types = type + 1;
    }
    if (cbdata_index[type].pool)
	return;
    label = xmalloc(strlen(name) + 20);
    snprintf(label, strlen(name) + 20, "cbdata %s (%d)", name, (int) type);
#if !HASHED_CBDATA
    assert(OFFSET_OF(cbdata, data) == (sizeof(cbdata) - sizeof(((cbdata *) NULL)->data)));
    cbdata_index[type].pool = memPoolCreate(label, size + OFFSET_OF(cbdata, data));
#else
    cbdata_index[type].pool = memPoolCreate(label, size);
#endif
    cbdata_index[type].free_func = free_func;
}

cbdata_type
cbdataAddType(cbdata_type type, const char *name, int size, FREE * free_func)
{
    if (type)
	return type;
    type = cbdata_types;
    cbdataInitType(type, name, size, free_func);
    return type;
}

void
cbdataInit(void)
{
    debugs(45, 3, "cbdataInit");
#if HASHED_CBDATA
    cbdata_pool = memPoolCreate("cbdata", sizeof(cbdata));
    cbdata_htable = hash_create(cbdata_cmp, 1 << 12, cbdata_hash);
#endif
    cbdata_types = CBDATA_FIRST_CUSTOM_TYPE + 1;
}


int
cbdataInUseCount(cbdata_type type)
{
	return memPoolInUseCount(cbdata_index[type].pool);
}

void *
#if CBDATA_DEBUG
cbdataInternalAllocDbg(cbdata_type type, const char *file, int line)
#else
cbdataInternalAlloc(cbdata_type type)
#endif
{
    cbdata *c;
    void *p;
    assert(type > 0 && type < cbdata_types);
#if HASHED_CBDATA
    c = memPoolAlloc(cbdata_pool);
    p = memPoolAlloc(cbdata_index[type].pool);
    c->hash.key = p;
    hash_join(cbdata_htable, &c->hash);
#else
    c = memPoolAlloc(cbdata_index[type].pool);
    p = (void *) &c->data;
#endif
    c->type = type;
    c->valid = 1;
    c->locks = 0;
#if CBDATA_DEBUG
    c->file = file;
    c->line = line;
#endif
    c->y = CBDATA_COOKIE(p);
    cbdataCount++;

    return p;
}

void *
cbdataInternalFree(void *p)
{
    cbdata *c;
    FREE *free_func;
    debugs(45, 3, "cbdataFree: %p", p);
#if HASHED_CBDATA
    c = (cbdata *) hash_lookup(cbdata_htable, p);
#else
    c = (cbdata *) (((char *) p) - OFFSET_OF(cbdata, data));
#endif
    assert(c->y == CBDATA_COOKIE(p));
    assert(c->valid);
    c->valid = 0;
    if (c->locks) {
	debugs(45, 3, "cbdataFree: %p has %d locks, not freeing",
	    p, c->locks);
	return NULL;
    }
    cbdataCount--;
    c->y = NULL;
    debugs(45, 3, "cbdataFree: Freeing %p", p);
    free_func = cbdata_index[c->type].free_func;
    if (free_func)
	free_func((void *) p);
#if HASHED_CBDATA
    hash_remove_link(cbdata_htable, &c->hash);
    memPoolFree(cbdata_index[c->type].pool, p);
    memPoolFree(cbdata_pool, c);
#else
    memPoolFree(cbdata_index[c->type].pool, c);
#endif
    return NULL;
}

int
cbdataLocked(const void *p)
{
    cbdata *c;
    assert(p);
#if HASHED_CBDATA
    c = (cbdata *) hash_lookup(cbdata_htable, p);
#else
    c = (cbdata *) (((char *) p) - OFFSET_OF(cbdata, data));
#endif
    assert(c->y == CBDATA_COOKIE(p));
    assert(c->locks || c->valid);

    debugs(45, 3, "cbdataLocked: %p = %d", p, c->locks);
    assert(c != NULL);
    return c->locks;
}

void
#if CBDATA_DEBUG
cbdataLockDbg(const void *p, const char *file, int line)
#else
cbdataLock(const void *p)
#endif
{
    cbdata *c;
    if (p == NULL)
	return;
#if HASHED_CBDATA
    c = (cbdata *) hash_lookup(cbdata_htable, p);
#else
    c = (cbdata *) (((char *) p) - OFFSET_OF(cbdata, data));
#endif
    assert(c->y == CBDATA_COOKIE(p));
    assert(c->locks || c->valid);
    debugs(45, 3, "cbdataLock: %p", p);
    assert(c != NULL);
    c->locks++;
#if CBDATA_DEBUG
    c->file = file;
    c->line = line;
#endif
}

void
#if CBDATA_DEBUG
cbdataUnlockDbg(const void *p, const char *file, int line)
#else
cbdataUnlock(const void *p)
#endif
{
    cbdata *c;
    FREE *free_func;
    if (p == NULL)
	return;
    debugs(45, 3, "cbdataUnlock: %p", p);
#if HASHED_CBDATA
    c = (cbdata *) hash_lookup(cbdata_htable, p);
#else
    c = (cbdata *) (((char *) p) - OFFSET_OF(cbdata, data));
#endif
    assert(c->y == CBDATA_COOKIE(p));
    assert(c->locks > 0);
    c->locks--;
#if CBDATA_DEBUG
    c->file = file;
    c->line = line;
#endif
    if (c->valid || c->locks)
	return;
    cbdataCount--;
    debugs(45, 3, "cbdataUnlock: Freeing %p", p);
    free_func = cbdata_index[c->type].free_func;
    if (free_func)
	free_func((void *) p);
#if HASHED_CBDATA
    hash_remove_link(cbdata_htable, &c->hash);
    memPoolFree(cbdata_index[c->type].pool, (void *) p);
    memPoolFree(cbdata_pool, c);
#else
    memPoolFree(cbdata_index[c->type].pool, c);
#endif
}

int
cbdataValid(const void *p)
{
    cbdata *c;
    if (p == NULL)
	return 1;		/* A NULL pointer cannot become invalid */
    debugs(45, 3, "cbdataValid: %p", p);
#if HASHED_CBDATA
    c = (cbdata *) hash_lookup(cbdata_htable, p);
#else
    c = (cbdata *) (((char *) p) - OFFSET_OF(cbdata, data));
#endif
    assert(c->y == CBDATA_COOKIE(p));
    assert(c->locks > 0);
    return c->valid;
}

