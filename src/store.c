
/*
 * $Id: store.c 14761 2010-08-24 08:23:27Z adrian.chadd $
 *
 * DEBUG: section 20    Storage Manager
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
#include "../libcore/strutil.h"
#include "store.h"
#include "store_vary.h"

const char *memStatusStr[] =
{
    "NOT_IN_MEMORY",
    "IN_MEMORY"
};

const char *pingStatusStr[] =
{
    "PING_NONE",
    "PING_WAITING",
    "PING_DONE"
};

const char *storeStatusStr[] =
{
    "STORE_OK",
    "STORE_PENDING"
};

const char *swapStatusStr[] =
{
    "SWAPOUT_NONE",
    "SWAPOUT_WRITING",
    "SWAPOUT_DONE"
};

extern OBJH storeIOStats;
extern ADD  AddStoreIoAction;
extern COL  StoreIoActionCollect;
extern OBJH storeIOStatsEx;

/*
 * local function prototypes
 */
static int storeEntryValidLength(const StoreEntry *);
static void storeGetMemSpace(int);
static void storeHashDelete(StoreEntry *);
static MemObject *new_MemObject(const char *);
static void destroy_MemObject(StoreEntry *);
static FREE destroy_StoreEntry;
static void storePurgeMem(StoreEntry *);
static void storeEntryReferenced(StoreEntry *);
static void storeEntryDereferenced(StoreEntry *);
static int getKeyCounter(void);
static int storeKeepInMemory(const StoreEntry *);
static OBJH storeCheckCachableStats;
static EVH storeLateRelease;

/*
 * local variables
 */
static Stack LateReleaseStack;
MemPool * pool_memobject = NULL;
MemPool * pool_storeentry = NULL;

#if URL_CHECKSUM_DEBUG
unsigned int
url_checksum(const char *url)
{
    unsigned int ck;
    SQUID_MD5_CTX M;
    static unsigned char digest[16];
    SQUID_MD5Init(&M);
    SQUID_MD5Update(&M, (unsigned char *) url, strlen(url));
    SQUID_MD5Final(digest, &M);
    xmemcpy(&ck, digest, sizeof(ck));
    return ck;
}
#endif

static MemObject *
new_MemObject(const char *url)
{
    MemObject *mem = memPoolAlloc(pool_memobject);
    mem->reply = httpReplyCreate();
    mem->url = xstrdup(url);
#if URL_CHECKSUM_DEBUG
    mem->chksum = url_checksum(mem->url);
#endif
    mem->object_sz = -1;
    mem->serverfd = -1;
    debugs(20, 3, "new_MemObject: returning %p", mem);
    return mem;
}

int
memHaveHeaders(const MemObject * mem)
{
    if (!mem)
	return 0;
    if (!mem->reply)
	return 0;
    if (mem->reply->pstate != psParsed)
	return 0;
    return 1;
}


StoreEntry *
new_StoreEntry(int mem_obj_flag, const char *url)
{
    StoreEntry *e = NULL;
    e = memPoolAlloc(pool_storeentry);
    if (mem_obj_flag)
	e->mem_obj = new_MemObject(url);
    debugs(20, 3, "new_StoreEntry: returning %p", e);
    e->expires = e->lastmod = e->lastref = e->timestamp = -1;
    e->swap_filen = -1;
    e->swap_dirn = -1;
    return e;
}

void
storeEntrySetStoreUrl(StoreEntry * e, const char *store_url)
{
    /* XXX eww, another strdup! */
    if (!e->mem_obj)
	return;
    safe_free(e->mem_obj->store_url);
    e->mem_obj->store_url = xstrdup(store_url);
}

static void
destroy_MemObject(StoreEntry * e)
{
    MemObject *mem = e->mem_obj;
    const Ctx ctx = ctx_enter(mem->url);
    debugs(20, 3, "destroy_MemObject: destroying %p", mem);
#if URL_CHECKSUM_DEBUG
    assert(mem->chksum == url_checksum(mem->url));
#endif
    e->mem_obj = NULL;
    urlMethodFree(mem->method);
    if (!shutting_down)
	assert(mem->swapout.sio == NULL);
    stmemFree(&mem->data_hdr);
    mem->inmem_hi = 0;
#if 0
    /*
     * There is no way to abort FD-less clients, so they might
     * still have mem->clients set.
     */
    assert(mem->clients.head == NULL);
#endif
    if (mem->ims_entry) {
	storeUnlockObject(mem->ims_entry);
	mem->ims_entry = NULL;
    }
    if (mem->old_entry) {
	storeUnlockObject(mem->old_entry);
	mem->old_entry = NULL;
    }
    httpReplyDestroy(mem->reply);
    requestUnlink(mem->request);
    mem->request = NULL;
    ctx_exit(ctx);		/* must exit before we free mem->url */
    safe_free(mem->url);
    safe_free(mem->store_url);
    safe_free(mem->vary_headers);
    safe_free(mem->vary_encoding);
    memPoolFree(pool_memobject, mem);
}

static void
destroy_StoreEntry(void *data)
{
    StoreEntry *e = data;
    debugs(20, 3, "destroy_StoreEntry: destroying %p", e);
    assert(e != NULL);
    if (e->mem_obj)
	destroy_MemObject(e);
    storeHashDelete(e);
    assert(e->hash.key == NULL);
    memPoolFree(pool_storeentry, e);
}

/* ----- INTERFACE BETWEEN STORAGE MANAGER AND HASH TABLE FUNCTIONS --------- */

void
storeHashInsert(StoreEntry * e, const cache_key * key)
{
    debugs(20, 3, "storeHashInsert: Inserting Entry %p key '%s'",
	e, storeKeyText(key));
    e->hash.key = storeKeyDup(key);
    hash_join(store_table, &e->hash);
}

static void
storeHashDelete(StoreEntry * e)
{
    hash_remove_link(store_table, &e->hash);
    storeKeyFree(e->hash.key);
    e->hash.key = NULL;
}

/* -------------------------------------------------------------------------- */


/* get rid of memory copy of the object */
/* Only call this if storeCheckPurgeMem(e) returns 1 */
static void
storePurgeMem(StoreEntry * e)
{
    if (e->mem_obj == NULL)
	return;
    debugs(20, 3, "storePurgeMem: Freeing memory-copy of %s",
	storeKeyText(e->hash.key));
    storeSetMemStatus(e, NOT_IN_MEMORY);
    destroy_MemObject(e);
    if (e->swap_status != SWAPOUT_DONE)
	storeRelease(e);
}

static void
storeEntryReferenced(StoreEntry * e)
{
    SwapDir *SD;

    /* Notify the fs that we're referencing this object again */
    if (e->swap_dirn > -1) {
	SD = INDEXSD(e->swap_dirn);
	if (SD->refobj)
	    SD->refobj(SD, e);
    }
    /* Notify the memory cache that we're referencing this object again */
    if (e->mem_obj) {
	if (mem_policy->Referenced)
	    mem_policy->Referenced(mem_policy, e, &e->mem_obj->repl);
    }
}

static void
storeEntryDereferenced(StoreEntry * e)
{
    SwapDir *SD;

    /* Notify the fs that we're not referencing this object any more */
    if (e->swap_filen > -1) {
	SD = INDEXSD(e->swap_dirn);
	if (SD->unrefobj != NULL)
	    SD->unrefobj(SD, e);
    }
    /* Notify the memory cache that we're not referencing this object any more */
    if (e->mem_obj) {
	if (mem_policy->Dereferenced)
	    mem_policy->Dereferenced(mem_policy, e, &e->mem_obj->repl);
    }
}

void
storeLockObjectDebug(StoreEntry * e, const char *file, const int line)
{
    e->lock_count++;
    debugs(20, 3, "storeLockObject: (%s:%d): key '%s' count=%d", file, line,
	storeKeyText(e->hash.key), (int) e->lock_count);
    e->lastref = squid_curtime;
    storeEntryReferenced(e);
}

void
storeReleaseRequest(StoreEntry * e)
{
    if (EBIT_TEST(e->flags, RELEASE_REQUEST))
	return;
    debugs(20, 3, "storeReleaseRequest: '%s'", storeKeyText(e->hash.key));
    EBIT_SET(e->flags, RELEASE_REQUEST);
    /*
     * Clear cachable flag here because we might get called before
     * anyone else even looks at the cachability flag.  Also, this
     * prevents httpMakePublic from really setting a public key.
     */
    EBIT_CLR(e->flags, ENTRY_CACHABLE);
    storeSetPrivateKey(e);
}

/* unlock object, return -1 if object get released after unlock
 * otherwise lock_count */
int
storeUnlockObjectDebug(StoreEntry * e, const char *file, const int line)
{
    e->lock_count--;
    debugs(20, 3, "storeUnlockObject: (%s:%d): key '%s' count=%d", file, line,
	storeKeyText(e->hash.key), e->lock_count);
    if (e->lock_count)
	return (int) e->lock_count;
    if (e->store_status == STORE_PENDING)
	EBIT_SET(e->flags, RELEASE_REQUEST);
    assert(storePendingNClients(e) == 0);
    if (EBIT_TEST(e->flags, RELEASE_REQUEST))
	storeRelease(e);
    else if (storeKeepInMemory(e)) {
	storeEntryDereferenced(e);
	storeSetMemStatus(e, IN_MEMORY);
	requestUnlink(e->mem_obj->request);
	e->mem_obj->request = NULL;
    } else {
	storePurgeMem(e);
	storeEntryDereferenced(e);
	if (EBIT_TEST(e->flags, KEY_PRIVATE))
	    debugs(20, 1, "WARNING: %s:%d: found KEY_PRIVATE", __FILE__, __LINE__);
    }
    return 0;
}

/* Lookup an object in the cache.
 * return just a reference to object, don't start swapping in yet. */
StoreEntry *
storeGet(const cache_key * key)
{
    StoreEntry *e = (StoreEntry *) hash_lookup(store_table, key);
    debugs(20, 3, "storeGet: %s -> %p", storeKeyText(key), e);
    return e;
}

StoreEntry *
storeGetPublic(const char *uri, const method_t * method)
{
    return storeGet(storeKeyPublic(uri, method));
}

StoreEntry *
storeGetPublicByCode(const char *uri, const method_code_t code)
{
    method_t *method;

    method = urlMethodGetKnownByCode(code);
    if (method == NULL) {
	return (NULL);
    }
    return storeGetPublic(uri, method);
}

StoreEntry *
storeGetPublicByRequestMethod(request_t * req, const method_t * method)
{
    if (req->vary) {
	/* Varying objects... */
	if (req->vary->key)
	    return storeGet(storeKeyScan(req->vary->key));
	else
	    return NULL;
    }
    return storeGet(storeKeyPublicByRequestMethod(req, method));
}

StoreEntry *
storeGetPublicByRequestMethodCode(request_t * req, const method_code_t code)
{
    method_t *method;

    method = urlMethodGetKnownByCode(code);
    if (method == NULL) {
	return (NULL);
    }
    return storeGetPublicByRequestMethod(req, method);
}

StoreEntry *
storeGetPublicByRequest(request_t * req)
{
    StoreEntry *e = storeGetPublicByRequestMethod(req, req->method);
    if (e == NULL && req->method->code == METHOD_HEAD)
	/* We can generate a HEAD reply from a cached GET object */
	e = storeGetPublicByRequestMethodCode(req, METHOD_GET);
    return e;
}

void
storePurgeEntriesByUrl(request_t * req, const char *url)
{
    int m, get_or_head_sent;
    method_t *method;
    StoreEntry *e;

    debugs(20, 5, "storePurgeEntriesByUrl: purging %s", url);
    get_or_head_sent = 0;

    for (m = METHOD_NONE; m < METHOD_OTHER; m++) {
	method = urlMethodGetKnownByCode(m);
	if (!method->flags.cachable) {
	    continue;
	}
	if ((m == METHOD_HEAD || m == METHOD_GET) && get_or_head_sent) {
	    continue;
	}
	e = storeGetPublic(url, method);
	if (e == NULL) {
#if USE_HTCP
	    if (m == METHOD_HEAD) {
		method = urlMethodGetKnownByCode(METHOD_GET);
	    }
	    neighborsHtcpClear(NULL, url, req, method, HTCP_CLR_INVALIDATION);
	    if (m == METHOD_GET || m == METHOD_HEAD) {
		get_or_head_sent = 1;
	    }
#endif
	    continue;
	}
	debugs(20, 5, "storePurgeEntriesByUrl: purging %s %s",
	    urlMethodGetConstStr(method), url);
#if USE_HTCP
	if (m == METHOD_HEAD) {
	    method = urlMethodGetKnownByCode(METHOD_GET);
	}
	neighborsHtcpClear(e, url, req, method, HTCP_CLR_INVALIDATION);
	if (m == METHOD_GET || m == METHOD_HEAD) {
	    get_or_head_sent = 1;
	}
#endif
	storeRelease(e);
    }
}

static int
getKeyCounter(void)
{
    static int key_counter = 0;
    if (++key_counter < 0)
	key_counter = 1;
    return key_counter;
}

void
storeSetPrivateKey(StoreEntry * e)
{
    const cache_key *newkey;
    MemObject *mem = e->mem_obj;
    if (e->hash.key && EBIT_TEST(e->flags, KEY_PRIVATE))
	return;			/* is already private */
    if (e->hash.key) {
	if (e->swap_filen > -1)
	    storeDirSwapLog(e, SWAP_LOG_DEL);
	storeHashDelete(e);
    }
    if (mem != NULL) {
	mem->id = getKeyCounter();
	newkey = storeKeyPrivate(mem->url, mem->method, mem->id);
    } else {
	newkey = storeKeyPrivate("JUNK", urlMethodGetKnown("NONE", 4), getKeyCounter());
    }
    assert(hash_lookup(store_table, newkey) == NULL);
    EBIT_SET(e->flags, KEY_PRIVATE);
    storeHashInsert(e, newkey);
}

void
storeSetPublicKey(StoreEntry * e)
{
    StoreEntry *e2 = NULL;
    const cache_key *newkey;
    MemObject *mem = e->mem_obj;
    const char *str = NULL;
    if (e->hash.key && !EBIT_TEST(e->flags, KEY_PRIVATE)) {
	if (EBIT_TEST(e->flags, KEY_EARLY_PUBLIC)) {
	    EBIT_CLR(e->flags, KEY_EARLY_PUBLIC);
	    storeSetPrivateKey(e);	/* wasn't really public yet, reset the key */
	} else {
	    return;		/* is already public */
	}
    }
    assert(mem);
    /*
     * We can't make RELEASE_REQUEST objects public.  Depending on
     * when RELEASE_REQUEST gets set, we might not be swapping out
     * the object.  If we're not swapping out, then subsequent
     * store clients won't be able to access object data which has
     * been freed from memory.
     *
     * If RELEASE_REQUEST is set, then ENTRY_CACHABLE should not
     * be set, and storeSetPublicKey() should not be called.
     */
#if MORE_DEBUG_OUTPUT
    if (EBIT_TEST(e->flags, RELEASE_REQUEST))
	debugs(20, 1, "assertion failed: RELEASE key %s, url %s",
	    e->hash.key, mem->url);
#endif
    assert(!EBIT_TEST(e->flags, RELEASE_REQUEST));
    if (mem->request) {
	StoreEntry *pe;
	request_t *request = mem->request;
	if (!mem->vary_headers) {
	    /* First handle the case where the object no longer varies */
	    safe_free(request->vary_headers);
	} else {
	    if (request->vary_headers && strcmp(request->vary_headers, mem->vary_headers) != 0) {
		/* Oops.. the variance has changed. Kill the base object
		 * to record the new variance key
		 */
		safe_free(request->vary_headers);	/* free old "bad" variance key */
		pe = storeGetPublic(storeLookupUrl(e), mem->method);
		if (pe)
		    storeRelease(pe);
	    }
	    /* Make sure the request knows the variance status */
	    else if (!request->vary_headers) {
		if (!httpMakeVaryMark(request, mem->reply)) {
		    /* Release the object if we could not index the variance */
		    storeReleaseRequest(e);
		    return;
		}
	    }
	}
	newkey = storeKeyPublicByRequest(mem->request);
	if (mem->vary_headers && !EBIT_TEST(e->flags, KEY_EARLY_PUBLIC)) {
	    String vary = StringNull;
	    String varyhdr;
	    varyhdr = httpHeaderGetList(&mem->reply->header, HDR_VARY);
	    if (strIsNotNull(varyhdr))
		strListAddStr(&vary, strBuf2(varyhdr), strLen2(varyhdr), ',');
	    stringClean(&varyhdr);
#if X_ACCELERATOR_VARY
	    /* This needs to match the order in http.c:httpMakeVaryMark */
	    varyhdr = httpHeaderGetList(&mem->reply->header, HDR_X_ACCELERATOR_VARY);
	    if (strIsNotNull(varyhdr))
		strListAddStr(&vary, strBuf2(varyhdr), strLen2(varyhdr), ',');
	    stringClean(&varyhdr);
#endif
	    str = stringDupToC(&vary);
	    storeAddVary(mem->store_url, mem->url, mem->method, newkey, httpHeaderGetStr(&mem->reply->header, HDR_ETAG), str, mem->vary_headers, mem->vary_encoding);
	    safe_free(str);
	    stringClean(&vary);
	}
    } else {
	newkey = storeKeyPublic(storeLookupUrl(e), mem->method);
    }
    if ((e2 = (StoreEntry *) hash_lookup(store_table, newkey))) {
	debugs(20, 3, "storeSetPublicKey: Making old '%s' private.", mem->url);
	storeSetPrivateKey(e2);
	storeRelease(e2);
	if (mem->request)
	    newkey = storeKeyPublicByRequest(mem->request);
	else
	    newkey = storeKeyPublic(storeLookupUrl(e), mem->method);
    }
    if (e->hash.key)
	storeHashDelete(e);
    EBIT_CLR(e->flags, KEY_PRIVATE);
    storeHashInsert(e, newkey);
    if (e->swap_filen > -1)
	storeDirSwapLog(e, SWAP_LOG_ADD);
}

StoreEntry *
storeCreateEntry(const char *url, request_flags flags, method_t * method)
{
    StoreEntry *e = NULL;
    MemObject *mem = NULL;
    debugs(20, 3, "storeCreateEntry: '%s'", url);

    e = new_StoreEntry(STORE_ENTRY_WITH_MEMOBJ, url);
    e->lock_count = 1;		/* Note lock here w/o calling storeLock() */
    mem = e->mem_obj;
    mem->method = urlMethodDup(method);
    if (neighbors_do_private_keys || !flags.hierarchical)
	storeSetPrivateKey(e);
    else
	storeSetPublicKey(e);
    if (flags.cachable) {
	EBIT_SET(e->flags, ENTRY_CACHABLE);
	EBIT_CLR(e->flags, RELEASE_REQUEST);
    } else {
	EBIT_CLR(e->flags, ENTRY_CACHABLE);
	storeReleaseRequest(e);
    }
    e->store_status = STORE_PENDING;
    storeSetMemStatus(e, NOT_IN_MEMORY);
    e->swap_status = SWAPOUT_NONE;
    e->swap_filen = -1;
    e->swap_dirn = -1;
    e->refcount = 0;
    e->lastref = squid_curtime;
    e->timestamp = -1;		/* set in storeTimestampsSet() */
    e->ping_status = PING_NONE;
    EBIT_SET(e->flags, ENTRY_VALIDATED);
    return e;
}

/* Mark object as expired */
void
storeExpireNow(StoreEntry * e)
{
    debugs(20, 3, "storeExpireNow: '%s'", storeKeyText(e->hash.key));
    e->expires = squid_curtime;
}

/* Append incoming data from a primary server to an entry. */
void
storeAppend(StoreEntry * e, const char *buf, int len)
{
    MemObject *mem = e->mem_obj;
    assert(mem != NULL);
    assert(len >= 0);
    assert(e->store_status == STORE_PENDING);
    mem->refresh_timestamp = squid_curtime;
    if (len) {
	debugs(20, 5, "storeAppend: appending %d bytes for '%s'",
	    len,
	    storeKeyText(e->hash.key));
	storeGetMemSpace(len);
	stmemAppend(&mem->data_hdr, buf, len);
	mem->inmem_hi += len;
    }
    if (EBIT_TEST(e->flags, DELAY_SENDING))
	return;
    InvokeHandlers(e);
    storeSwapOut(e);
}

void
#if STDC_HEADERS
storeAppendPrintf(StoreEntry * e, const char *fmt,...)
#else
storeAppendPrintf(va_alist)
     va_dcl
#endif
{
#if STDC_HEADERS
    va_list args;
    va_start(args, fmt);
#else
    va_list args;
    StoreEntry *e = NULL;
    const char *fmt = NULL;
    va_start(args);
    e = va_arg(args, StoreEntry *);
    fmt = va_arg(args, char *);
#endif
    storeAppendVPrintf(e, fmt, args);
    va_end(args);
}

/* used be storeAppendPrintf and Packer */
void
storeAppendVPrintf(StoreEntry * e, const char *fmt, va_list vargs)
{
    LOCAL_ARRAY(char, buf, 4096);
    buf[0] = '\0';
    vsnprintf(buf, 4096, fmt, vargs);
    storeAppend(e, buf, strlen(buf));
}

struct _store_check_cachable_hist {
    struct {
	int non_get;
	int not_entry_cachable;
	int release_request;
	int wrong_content_length;
	int negative_cached;
	int too_big;
	int too_small;
	int private_key;
	int too_many_open_files;
	int too_many_open_fds;
    } no;
    struct {
	int Default;
    } yes;
} store_check_cachable_hist;

int
storeTooManyDiskFilesOpen(void)
{
    if (Config.max_open_disk_fds == 0)
	return 0;
    if (store_open_disk_fd > Config.max_open_disk_fds)
	return 1;
    return 0;
}

static int
storeCheckTooSmall(StoreEntry * e)
{
    MemObject *mem = e->mem_obj;
    if (EBIT_TEST(e->flags, ENTRY_SPECIAL))
	return 0;
    if (STORE_OK == e->store_status)
	if (mem->object_sz < Config.Store.minObjectSize)
	    return 1;
    if (mem->reply->content_length > -1)
	if (mem->reply->content_length < Config.Store.minObjectSize)
	    return 1;
    return 0;
}

int
storeCheckCachable(StoreEntry * e)
{
#if CACHE_ALL_METHODS
    if (e->mem_obj->method != METHOD_GET) {
	debugs(20, 2, "storeCheckCachable: NO: non-GET method");
	store_check_cachable_hist.no.non_get++;
    } else
#endif
    if (e->store_status == STORE_OK && EBIT_TEST(e->flags, ENTRY_BAD_LENGTH)) {
	debugs(20, 2, "storeCheckCachable: NO: wrong content-length");
	store_check_cachable_hist.no.wrong_content_length++;
    } else if (EBIT_TEST(e->flags, RELEASE_REQUEST)) {
	debugs(20, 2, "storeCheckCachable: NO: release requested");
	store_check_cachable_hist.no.release_request++;
    } else if (!EBIT_TEST(e->flags, ENTRY_CACHABLE)) {
	debugs(20, 2, "storeCheckCachable: NO: not cachable");
	store_check_cachable_hist.no.not_entry_cachable++;
    } else if (EBIT_TEST(e->flags, ENTRY_NEGCACHED)) {
	debugs(20, 3, "storeCheckCachable: NO: negative cached");
	store_check_cachable_hist.no.negative_cached++;
	return 0;		/* avoid release call below */
    } else if ((e->mem_obj->reply->content_length > 0 &&
		e->mem_obj->reply->content_length > Config.Store.maxObjectSize) ||
	e->mem_obj->inmem_hi > Config.Store.maxObjectSize) {
	debugs(20, 2, "storeCheckCachable: NO: too big");
	store_check_cachable_hist.no.too_big++;
    } else if (storeCheckTooSmall(e)) {
	debugs(20, 2, "storeCheckCachable: NO: too small");
	store_check_cachable_hist.no.too_small++;
    } else if (EBIT_TEST(e->flags, KEY_PRIVATE)) {
	debugs(20, 3, "storeCheckCachable: NO: private key");
	store_check_cachable_hist.no.private_key++;
    } else if (e->swap_status != SWAPOUT_NONE) {
	/*
	 * here we checked the swap_status because the remaining
	 * cases are only relevant only if we haven't started swapping
	 * out the object yet.
	 */
	return 1;
    } else if (storeTooManyDiskFilesOpen()) {
	debugs(20, 2, "storeCheckCachable: NO: too many disk files open");
	store_check_cachable_hist.no.too_many_open_files++;
    } else if (fdNFree() < RESERVED_FD) {
	debugs(20, 2, "storeCheckCachable: NO: too many FD's open");
	store_check_cachable_hist.no.too_many_open_fds++;
    } else {
	store_check_cachable_hist.yes.Default++;
	return 1;
    }
    storeReleaseRequest(e);
    EBIT_CLR(e->flags, ENTRY_CACHABLE);
    return 0;
}

static void
storeCheckCachableStats(StoreEntry * sentry, void* data)
{
    storeAppendPrintf(sentry, "Category\t Count\n");

#if CACHE_ALL_METHODS
    storeAppendPrintf(sentry, "no.non_get\t%d\n",
	store_check_cachable_hist.no.non_get);
#endif
    storeAppendPrintf(sentry, "no.not_entry_cachable\t%d\n",
	store_check_cachable_hist.no.not_entry_cachable);
    storeAppendPrintf(sentry, "no.release_request\t%d\n",
	store_check_cachable_hist.no.release_request);
    storeAppendPrintf(sentry, "no.wrong_content_length\t%d\n",
	store_check_cachable_hist.no.wrong_content_length);
    storeAppendPrintf(sentry, "no.negative_cached\t%d\n",
	store_check_cachable_hist.no.negative_cached);
    storeAppendPrintf(sentry, "no.too_big\t%d\n",
	store_check_cachable_hist.no.too_big);
    storeAppendPrintf(sentry, "no.too_small\t%d\n",
	store_check_cachable_hist.no.too_small);
    storeAppendPrintf(sentry, "no.private_key\t%d\n",
	store_check_cachable_hist.no.private_key);
    storeAppendPrintf(sentry, "no.too_many_open_files\t%d\n",
	store_check_cachable_hist.no.too_many_open_files);
    storeAppendPrintf(sentry, "no.too_many_open_fds\t%d\n",
	store_check_cachable_hist.no.too_many_open_fds);
    storeAppendPrintf(sentry, "yes.default\t%d\n",
	store_check_cachable_hist.yes.Default);
}

/* Complete transfer into the local cache.  */
void
storeComplete(StoreEntry * e)
{
    debugs(20, 3, "storeComplete: '%s'", storeKeyText(e->hash.key));
    if (e->store_status != STORE_PENDING) {
	/*
	 * if we're not STORE_PENDING, then probably we got aborted
	 * and there should be NO clients on this entry
	 */
	assert(EBIT_TEST(e->flags, ENTRY_ABORTED));
	assert(e->mem_obj->nclients == 0);
	return;
    }
    e->mem_obj->object_sz = e->mem_obj->inmem_hi;
    e->store_status = STORE_OK;
    assert(e->mem_status == NOT_IN_MEMORY);
    if (!storeEntryValidLength(e)) {
	EBIT_SET(e->flags, ENTRY_BAD_LENGTH);
	storeReleaseRequest(e);
    }
#if USE_CACHE_DIGESTS
    if (e->mem_obj->request)
	e->mem_obj->request->hier.store_complete_stop = current_time;
#endif
    e->mem_obj->refresh_timestamp = 0;
    if (e->mem_obj->old_entry) {
	if (e->mem_obj->old_entry->mem_obj)
	    e->mem_obj->old_entry->mem_obj->refresh_timestamp = 0;
    }
    /*
     * We used to call InvokeHandlers, then storeSwapOut.  However,
     * Madhukar Reddy <myreddy@persistence.com> reported that
     * responses without content length would sometimes get released
     * in client_side, thinking that the response is incomplete.
     */
    storeSwapOut(e);
    InvokeHandlers(e);
}

/* Aborted transfer into the local cache. */
/* This takes ownership of ErrorState *err */
void
storeRequestFailed(StoreEntry * e, ErrorState * err)
{
    MemObject *mem = e->mem_obj;
    assert(e->store_status == STORE_PENDING);
    assert(mem != NULL);
    debugs(20, 6, "storeAbort: %s", storeKeyText(e->hash.key));
    storeLockObject(e);		/* lock while aborting */
    storeExpireNow(e);
    storeReleaseRequest(e);
    storeSetMemStatus(e, NOT_IN_MEMORY);
    if (e->mem_obj->inmem_hi == 0) {
	assert(err);
	errorAppendEntry(e, err);
	err = NULL;
    } else {
	EBIT_SET(e->flags, ENTRY_ABORTED);
	EBIT_CLR(e->flags, ENTRY_FWD_HDR_WAIT);
    }
    if (err)
	errorStateFree(err);	
    e->store_status = STORE_OK;
    mem->object_sz = mem->inmem_hi;
    /* Notify the client side */
    InvokeHandlers(e);
    /* Close any swapout file */
    storeSwapOutFileClose(e);
    storeUnlockObject(e);	/* unlock */
}

/*
 * Someone wants to abort this transfer.  Set the reason in the
 * request structure, call the server-side callback and mark the
 * entry for releasing
 */
void
storeAbort(StoreEntry * e)
{
    MemObject *mem = e->mem_obj;
    assert(e->store_status == STORE_PENDING);
    assert(mem != NULL);
    debugs(20, 6, "storeAbort: %s", storeKeyText(e->hash.key));
    storeLockObject(e);		/* lock while aborting */
    storeExpireNow(e);
    storeReleaseRequest(e);
    EBIT_SET(e->flags, ENTRY_ABORTED);
    storeSetMemStatus(e, NOT_IN_MEMORY);
    e->store_status = STORE_OK;
    /*
     * We assign an object length here.  The only other place we assign
     * the object length is in storeComplete()
     */
    mem->object_sz = mem->inmem_hi;
    /* Notify the server side */
    if (mem->abort.callback) {
	eventAdd("mem->abort.callback",
	    mem->abort.callback,
	    mem->abort.data,
	    0.0,
	    0);
	mem->abort.callback = NULL;
	mem->abort.data = NULL;
    }
    /* Notify the client side */
    InvokeHandlers(e);
    /* Close any swapout file */
    storeSwapOutFileClose(e);
    storeUnlockObject(e);	/* unlock */
}

/* Clear Memory storage to accommodate the given object len */
static void
storeGetMemSpace(int size)
{
    StoreEntry *e = NULL;
    int released = 0;
    static time_t last_check = 0;
    int pages_needed;
    RemovalPurgeWalker *walker;
    if (squid_curtime == last_check)
	return;
    last_check = squid_curtime;
    pages_needed = (size / SM_PAGE_SIZE) + 1;
    if (memPoolInUseCount(pool_mem_node) + pages_needed < store_pages_max)
	return;
    debugs(20, 3, "storeGetMemSpace: Starting, need %d pages", pages_needed);
    /* XXX what to set as max_scan here? */
    walker = mem_policy->PurgeInit(mem_policy, 100000);
    while ((e = walker->Next(walker))) {
	debugs(20, 3, "storeGetMemSpace: purging %p", e);
	storePurgeMem(e);
	released++;
	if (memPoolInUseCount(pool_mem_node) + pages_needed < store_pages_max) {
	    debugs(20, 3, "storeGetMemSpace: we finally have enough free memory!");
	    break;
	}
    }
    walker->Done(walker);
    debugs(20, 3, "storeGetMemSpace stats:");
    debugs(20, 3, "  %6d HOT objects", hot_obj_count);
    debugs(20, 3, "  %6d were released", released);
}

/* The maximum objects to scan for maintain storage space */
#define MAINTAIN_MAX_SCAN	1024
#define MAINTAIN_MAX_REMOVE	64

/*
 * This routine is to be called by main loop in main.c.
 * It removes expired objects on only one bucket for each time called.
 * returns the number of objects removed
 *
 * This should get called 1/s from main().
 */
void
storeMaintainSwapSpace(void *datanotused)
{
    int i;
    SwapDir *SD;
    static time_t last_warn_time = 0;

    /* walk each fs */
    for (i = 0; i < Config.cacheSwap.n_configured; i++) {
	/* call the maintain function .. */
	SD = INDEXSD(i);
	/* XXX FixMe: This should be done "in parallell" on the different
	 * cache_dirs, not one at a time.
	 */
	if (SD->maintainfs != NULL)
	    SD->maintainfs(SD);
    }
    if (store_swap_size > Config.Swap.maxSize) {
	if (squid_curtime - last_warn_time > 10) {
	    debugs(20, 0, "WARNING: Disk space over limit: %ld KB > %ld KB",
		(long int) store_swap_size, (long int) Config.Swap.maxSize);
	    last_warn_time = squid_curtime;
	}
    }
    /* Reregister a maintain event .. */
    eventAdd("MaintainSwapSpace", storeMaintainSwapSpace, NULL, 1.0, 1);
}


/* release an object from a cache */
void
storeRelease(StoreEntry * e)
{
    debugs(20, 3, "storeRelease: Releasing: '%s'", storeKeyText(e->hash.key));
    /* If, for any reason we can't discard this object because of an
     * outstanding request, mark it for pending release */
    if (storeEntryLocked(e)) {
	storeExpireNow(e);
	debugs(20, 3, "storeRelease: Only setting RELEASE_REQUEST bit");
	storeReleaseRequest(e);
	return;
    }
    if (store_dirs_rebuilding && e->swap_filen > -1) {
	storeSetPrivateKey(e);
	if (e->mem_obj) {
	    storeSetMemStatus(e, NOT_IN_MEMORY);
	    destroy_MemObject(e);
	}
	if (e->swap_filen > -1) {
	    /*
	     * Fake a call to storeLockObject().  When rebuilding is done,
	     * we'll just call storeUnlockObject() on these.
	     */
	    e->lock_count++;
	    EBIT_SET(e->flags, RELEASE_REQUEST);
	    stackPush(&LateReleaseStack, e);
	    return;
	} else {
	    destroy_StoreEntry(e);
	}
    }
    storeLog(STORE_LOG_RELEASE, e);
    if (e->swap_filen > -1) {
	storeUnlink(e);
	if (e->swap_status == SWAPOUT_DONE)
	    if (EBIT_TEST(e->flags, ENTRY_VALIDATED))
		storeDirUpdateSwapSize(&Config.cacheSwap.swapDirs[e->swap_dirn], e->swap_file_sz, -1);
	if (!EBIT_TEST(e->flags, KEY_PRIVATE))
	    storeDirSwapLog(e, SWAP_LOG_DEL);
#if 0
	/* From 2.4. I think we do this in storeUnlink? */
	storeSwapFileNumberSet(e, -1);
#endif
    }
    storeSetMemStatus(e, NOT_IN_MEMORY);
    destroy_StoreEntry(e);
}

static void
storeLateRelease(void *unused)
{
    StoreEntry *e;
    int i;
    static int n = 0;
    if (store_dirs_rebuilding) {
	eventAdd("storeLateRelease", storeLateRelease, NULL, 1.0, 1);
	return;
    }
    for (i = 0; i < 10; i++) {
	e = stackPop(&LateReleaseStack);
	if (e == NULL) {
	    /* done! */
	    debugs(20, 1, "storeLateRelease: released %d objects", n);
	    return;
	}
	storeUnlockObject(e);
	n++;
    }
    eventAdd("storeLateRelease", storeLateRelease, NULL, 0.0, 1);
}

/* return 1 if a store entry is locked */
int
storeEntryLocked(const StoreEntry * e)
{
    if (e->lock_count)
	return 1;
    if (e->swap_status == SWAPOUT_WRITING)
	return 1;
    if (e->store_status == STORE_PENDING)
	return 1;
    /*
     * SPECIAL, PUBLIC entries should be "locked"
     */
    if (EBIT_TEST(e->flags, ENTRY_SPECIAL))
	if (!EBIT_TEST(e->flags, KEY_PRIVATE))
	    return 1;
    return 0;
}

static int
storeEntryValidLength(const StoreEntry * e)
{
    squid_off_t diff;
    squid_off_t clen;
    const HttpReply *reply;
    assert(e->mem_obj != NULL);
    reply = e->mem_obj->reply;
    debugs(20, 3, "storeEntryValidLength: Checking '%s'", storeKeyText(e->hash.key));
    debugs(20, 5, "storeEntryValidLength:     object_len = %" PRINTF_OFF_T "",
	objectLen(e));
    debugs(20, 5, "storeEntryValidLength:         hdr_sz = %d",
	reply->hdr_sz);
    clen = httpReplyBodySize(e->mem_obj->method, reply);
    debugs(20, 5, "storeEntryValidLength: content_length = %" PRINTF_OFF_T "",
	clen);
    if (clen < 0) {
	debugs(20, 5, "storeEntryValidLength: Unspecified content length: %s",
	    storeKeyText(e->hash.key));
	return 1;
    }
    diff = reply->hdr_sz + clen - objectLen(e);
    if (diff == 0)
	return 1;
    debugs(20, 2, "storeEntryValidLength: %" PRINTF_OFF_T " bytes too %s; '%s'",
	diff < 0 ? -diff : diff,
	diff < 0 ? "big" : "small",
	storeKeyText(e->hash.key));
    return 0;
}

static void
storeInitHashValues(void)
{
    long int i;
    /* Calculate size of hash table (maximum currently 64k buckets).  */
    i = (Config.Swap.maxSize + (Config.memMaxSize >> 10)) / Config.Store.avgObjectSize;
    debugs(20, 1, "Swap maxSize %lu + %lu KB, estimated %ld objects",
	(unsigned long int) Config.Swap.maxSize, (long int) (Config.memMaxSize >> 10), (unsigned long int) i);
    i /= Config.Store.objectsPerBucket;
    debugs(20, 1, "Target number of buckets: %ld", i);
    /* ideally the full scan period should be configurable, for the
     * moment it remains at approximately 24 hours.  */
    store_hash_buckets = storeKeyHashBuckets(i);
    debugs(20, 1, "Using %d Store buckets", store_hash_buckets);
    debugs(20, 1, "Max Mem  size: %lu KB", (unsigned long int) (Config.memMaxSize >> 10));
    debugs(20, 1, "Max Swap size: %lu KB", (unsigned long int) Config.Swap.maxSize);
}

void
storeInitMem(void)
{
    pool_storeentry = memPoolCreate("StoreEntry", sizeof(StoreEntry));
    pool_memobject = memPoolCreate("MemObject", sizeof(MemObject));
}

void
storeInit(void)
{
    storeKeyInit();
    storeInitHashValues();
    store_table = hash_create(storeKeyHashCmp, store_hash_buckets, storeKeyHashHash);
    mem_policy = createRemovalPolicy(Config.memPolicy);
    storeDigestInit();
    storeLogOpen();
    stackInit(&LateReleaseStack);
    eventAdd("storeLateRelease", storeLateRelease, NULL, 1.0, 1);
    storeDirInit();
    storeRebuildStart();
    cachemgrRegister("storedir",
	"Store Directory Stats",
	storeDirStats, NULL, NULL, 0, 1, 0);
    cachemgrRegister("store_check_cachable_stats",
	"storeCheckCachable() Stats",
	storeCheckCachableStats, NULL, NULL, 0, 1,0);
    cachemgrRegister("store_io",
	"Store IO Interface Stats",
	storeIOStatsEx, AddStoreIoAction, StoreIoActionCollect, 0, 1, 1);
}

void
storeConfigure(void)
{
    store_swap_high = (long) (((float) Config.Swap.maxSize *
	    (float) Config.Swap.highWaterMark) / (float) 100);
    store_swap_low = (long) (((float) Config.Swap.maxSize *
	    (float) Config.Swap.lowWaterMark) / (float) 100);
    store_pages_max = Config.memMaxSize / SM_PAGE_SIZE;
}

static int
storeKeepInMemory(const StoreEntry * e)
{
    MemObject *mem = e->mem_obj;
    if (mem == NULL)
	return 0;
    if (mem->data_hdr.head == NULL)
	return 0;
    return mem->inmem_lo == 0;
}

void
storeNegativeCache(StoreEntry * e)
{
    StoreEntry *oe = e->mem_obj->old_entry;
    time_t expires = e->expires;
    http_status status = e->mem_obj->reply->sline.status;
    refresh_cc cc = refreshCC(e, e->mem_obj->request);
    if (expires == -1)
	expires = squid_curtime + cc.negative_ttl;
    if (status && oe && !EBIT_TEST(oe->flags, KEY_PRIVATE) && !EBIT_TEST(oe->flags, ENTRY_REVALIDATE) &&
	500 <= status && status <= 504) {
	HttpHdrCc *oldcc = oe->mem_obj->reply->cache_control;
	if (oldcc && EBIT_TEST(oldcc->mask, CC_STALE_IF_ERROR) && oldcc->stale_if_error >= 0)
	    cc.max_stale = oldcc->stale_if_error;
	if (cc.max_stale >= 0) {
	    time_t max_expires;
	    storeTimestampsSet(oe);
	    max_expires = oe->expires + cc.max_stale;
	    /* Bail out if beyond the stale-if-error staleness limit */
	    if (max_expires <= squid_curtime)
		goto cache_error_response;
	    /* Limit expiry time to stale-if-error/max_stale */
	    if (expires > max_expires)
		expires = max_expires;
	}
	/* Block the new error from getting cached */
	EBIT_CLR(e->flags, ENTRY_CACHABLE);
	/* And negatively cache the old one */
	if (oe->expires < expires)
	    oe->expires = expires;
	EBIT_SET(oe->flags, REFRESH_FAILURE);
	return;
    }
  cache_error_response:
    if (e->expires < expires)
	e->expires = expires;
    EBIT_SET(e->flags, ENTRY_NEGCACHED);
}

void
storeFreeMemory(void)
{
    hashFreeItems(store_table, destroy_StoreEntry);
    hashFreeMemory(store_table);
    store_table = NULL;
#if USE_CACHE_DIGESTS
    if (store_digest)
	cacheDigestDestroy(store_digest);
#endif
    store_digest = NULL;
}

int
expiresMoreThan(time_t expires, time_t when)
{
    if (expires < 0)		/* No Expires given */
	return 1;
    return (expires > (squid_curtime + when));
}

int
storeEntryValidToSend(StoreEntry * e)
{
    if (EBIT_TEST(e->flags, RELEASE_REQUEST))
	return 0;
    if (EBIT_TEST(e->flags, ENTRY_NEGCACHED))
	if (e->expires <= squid_curtime)
	    return 0;
    if (EBIT_TEST(e->flags, ENTRY_ABORTED))
	return 0;
    /* Entries which seem to have got stuck is not valid to send to new clients */
    if (e->store_status == STORE_PENDING) {
	if (!e->mem_obj || e->mem_obj->refresh_timestamp + Config.collapsed_forwarding_timeout < squid_curtime)
	    return 0;
	else
	    return -1;
    }
    return 1;
}

void
storeTimestampsSet(StoreEntry * entry)
{
    const HttpReply *reply = entry->mem_obj->reply;
    time_t served_date = reply->date;
    int age = httpHeaderGetInt(&reply->header, HDR_AGE);
    /*
     * The timestamp calculations below tries to mimic the properties
     * of the age calculation in RFC2616 section 13.2.3. The implementaion
     * isn't complete, and the most notable exception from the RFC is that
     * this does not account for response_delay, but it probably does
     * not matter much as this is calculated immediately when the headers
     * are received, not when the whole response has been received.
     */
    /* make sure that 0 <= served_date <= squid_curtime */
    if (served_date < 0 || served_date > squid_curtime)
	served_date = squid_curtime;
    /*
     * Compensate with Age header if origin server clock is ahead
     * of us and there is a cache in between us and the origin
     * server.  But DONT compensate if the age value is larger than
     * squid_curtime because it results in a negative served_date.
     */
    if (age > squid_curtime - served_date)
	if (squid_curtime > age)
	    served_date = squid_curtime - age;
    if (reply->expires > 0 && reply->date > -1)
	entry->expires = served_date + (reply->expires - reply->date);
    else
	entry->expires = reply->expires;
    entry->lastmod = reply->last_modified;
    entry->timestamp = served_date;
}

void
storeRegisterAbort(StoreEntry * e, STABH * cb, void *data)
{
    MemObject *mem = e->mem_obj;
    assert(mem);
    assert(mem->abort.callback == NULL);
    mem->abort.callback = cb;
    mem->abort.data = data;
}

void
storeUnregisterAbort(StoreEntry * e)
{
    MemObject *mem = e->mem_obj;
    assert(mem);
    mem->abort.callback = NULL;
}

void
storeMemObjectDump(MemObject * mem)
{
    debugs(20, 1, "MemObject->data.head: %p",
	mem->data_hdr.head);
    debugs(20, 1, "MemObject->data.tail: %p",
	mem->data_hdr.tail);
    debugs(20, 1, "MemObject->data.origin_offset: %" PRINTF_OFF_T "",
	mem->data_hdr.origin_offset);
    debugs(20, 1, "MemObject->start_ping: %ld.%06d",
	(long int) mem->start_ping.tv_sec,
	(int) mem->start_ping.tv_usec);
    debugs(20, 1, "MemObject->inmem_hi: %" PRINTF_OFF_T "",
	mem->inmem_hi);
    debugs(20, 1, "MemObject->inmem_lo: %" PRINTF_OFF_T "",
	mem->inmem_lo);
    debugs(20, 1, "MemObject->nclients: %d",
	mem->nclients);
    debugs(20, 1, "MemObject->reply: %p",
	mem->reply);
    debugs(20, 1, "MemObject->request: %p",
	mem->request);
    debugs(20, 1, "MemObject->url: %p %s",
	mem->url,
	checkNullString(mem->url));
}

void
storeEntryDump(const StoreEntry * e, int l)
{
    debugs(20, l, "StoreEntry->key: %s", storeKeyText(e->hash.key));
    debugs(20, l, "StoreEntry->next: %p", e->hash.next);
    debugs(20, l, "StoreEntry->mem_obj: %p", e->mem_obj);
    debugs(20, l, "StoreEntry->timestamp: %ld", (long int) e->timestamp);
    debugs(20, l, "StoreEntry->lastref: %ld", (long int) e->lastref);
    debugs(20, l, "StoreEntry->expires: %ld", (long int) e->expires);
    debugs(20, l, "StoreEntry->lastmod: %ld", (long int) e->lastmod);
    debugs(20, l, "StoreEntry->swap_file_sz: %" PRINTF_OFF_T "", (squid_off_t) e->swap_file_sz);
    debugs(20, l, "StoreEntry->refcount: %d", e->refcount);
    debugs(20, l, "StoreEntry->flags: %s", storeEntryFlags(e));
    debugs(20, l, "StoreEntry->swap_dirn: %d", (int) e->swap_dirn);
    debugs(20, l, "StoreEntry->swap_filen: %d", (int) e->swap_filen);
    debugs(20, l, "StoreEntry->lock_count: %d", (int) e->lock_count);
    debugs(20, l, "StoreEntry->mem_status: %d", (int) e->mem_status);
    debugs(20, l, "StoreEntry->ping_status: %d", (int) e->ping_status);
    debugs(20, l, "StoreEntry->store_status: %d", (int) e->store_status);
    debugs(20, l, "StoreEntry->swap_status: %d", (int) e->swap_status);
}

/*
 * NOTE, this function assumes only two mem states
 */
void
storeSetMemStatus(StoreEntry * e, int new_status)
{
    MemObject *mem = e->mem_obj;
    if (new_status == e->mem_status)
	return;

	// are we using a shared memory cache?
    if (Config.memShared && IamWorkerProcess()) {
        // This method was designed to update replacement policy, not to
        // actually purge something from the memory cache (TODO: rename?).
        // Shared memory cache does not have a policy that needs updates.
        e->mem_status = new_status;
        return;
    }
	
    assert(mem != NULL);
    if (new_status == IN_MEMORY) {
	assert(mem->inmem_lo == 0);
	if (EBIT_TEST(e->flags, ENTRY_SPECIAL)) {
	    debugs(20, 4, "storeSetMemStatus: not inserting special %s into policy",
		mem->url);
	} else {
	    mem_policy->Add(mem_policy, e, &mem->repl);
	    debugs(20, 4, "storeSetMemStatus: inserted mem node %s",
		mem->url);
	}
	hot_obj_count++;
    } else {
	if (EBIT_TEST(e->flags, ENTRY_SPECIAL)) {
	    debugs(20, 4, "storeSetMemStatus: special entry %s",
		mem->url);
	} else {
	    mem_policy->Remove(mem_policy, e, &mem->repl);
	    debugs(20, 4, "storeSetMemStatus: removed mem node %s",
		mem->url);
	}
	hot_obj_count--;
    }
    e->mem_status = new_status;
}

const char *
storeUrl(const StoreEntry * e)
{
    if (e == NULL)
	return "[null_entry]";
    else if (e->mem_obj == NULL)
	return "[null_mem_obj]";
    else
	return e->mem_obj->url;
}

const char *
storeLookupUrl(const StoreEntry * e)
{
    if (e == NULL)
	return "[null_entry]";
    else if (e->mem_obj == NULL)
	return "[null_mem_obj]";
    else if (e->mem_obj->store_url)
	return e->mem_obj->store_url;
    else
	return e->mem_obj->url;
}

void
storeCreateMemObject(StoreEntry * e, const char *url)
{
    if (e->mem_obj)
	return;
    e->mem_obj = new_MemObject(url);
}

/* this just sets DELAY_SENDING */
void
storeBuffer(StoreEntry * e)
{
    EBIT_SET(e->flags, DELAY_SENDING);
}

/* this just clears DELAY_SENDING and Invokes the handlers */
void
storeBufferFlush(StoreEntry * e)
{
    if (EBIT_TEST(e->flags, DELAY_SENDING)) {
	EBIT_CLR(e->flags, DELAY_SENDING);
	InvokeHandlers(e);
	storeSwapOut(e);
    }
}

squid_off_t
objectLen(const StoreEntry * e)
{
    assert(e->mem_obj != NULL);
    return e->mem_obj->object_sz;
}

squid_off_t
contentLen(const StoreEntry * e)
{
    assert(e->mem_obj != NULL);
    assert(e->mem_obj->reply != NULL);
    return e->mem_obj->object_sz - e->mem_obj->reply->hdr_sz;
}

HttpReply *
storeEntryReply(StoreEntry * e)
{
    if (NULL == e)
	return NULL;
    if (NULL == e->mem_obj)
	return NULL;
    return e->mem_obj->reply;
}

void
storeEntryReset(StoreEntry * e)
{
    MemObject *mem = e->mem_obj;
    debugs(20, 3, "storeEntryReset: %s", storeUrl(e));
    assert(mem->swapout.sio == NULL);
    stmemFree(&mem->data_hdr);
    mem->inmem_hi = mem->inmem_lo = 0;
    httpReplyDestroy(mem->reply);
    mem->reply = httpReplyCreate();
    e->expires = e->lastmod = e->timestamp = -1;
}

/*
 * storeFsInit
 *
 * This routine calls the SETUP routine for each fs type.
 * I don't know where the best place for this is, and I'm not going to shuffle
 * around large chunks of code right now (that can be done once its working.)
 */
void
storeFsInit(void)
{
    storeReplSetup();
    storeFsSetup();
}


/*
 * similar to above, but is called when a graceful shutdown is to occur
 * of each fs module.
 */
void
storeFsDone(void)
{
    int i = 0;

    while (storefs_list[i].typestr != NULL) {
	storefs_list[i].donefunc();
	i++;
    }
}

/*
 * called to add another store fs module
 */
void
storeFsAdd(const char *type, STSETUP * setup)
{
    int i;
    /* find the number of currently known storefs types */
    for (i = 0; storefs_list && storefs_list[i].typestr; i++) {
	assert(strcmp(storefs_list[i].typestr, type) != 0);
    }
    /* add the new type */
    storefs_list = xrealloc(storefs_list, (i + 2) * sizeof(storefs_entry_t));
    memset(&storefs_list[i + 1], 0, sizeof(storefs_entry_t));
    storefs_list[i].typestr = type;
    /* Call the FS to set up capabilities and initialize the FS driver */
    setup(&storefs_list[i]);
}

/*
 * called to add another store removal policy module
 */
void
storeReplAdd(const char *type, REMOVALPOLICYCREATE * create)
{
    int i;
    /* find the number of currently known repl types */
    for (i = 0; storerepl_list && storerepl_list[i].typestr; i++) {
	assert(strcmp(storerepl_list[i].typestr, type) != 0);
    }
    /* add the new type */
    storerepl_list = xrealloc(storerepl_list, (i + 2) * sizeof(storerepl_entry_t));
    memset(&storerepl_list[i + 1], 0, sizeof(storerepl_entry_t));
    storerepl_list[i].typestr = type;
    storerepl_list[i].create = create;
}

/*
 * Create a removal policy instance
 */
RemovalPolicy *
createRemovalPolicy(RemovalPolicySettings * settings)
{
    storerepl_entry_t *r;
    for (r = storerepl_list; r && r->typestr; r++) {
	if (strcmp(r->typestr, settings->type) == 0)
	    return r->create(settings->args);
    }
    debugs(20, 1, "ERROR: Unknown policy %s", settings->type);
    debugs(20, 1, "ERROR: Be sure to have set cache_replacement_policy");
    debugs(20, 1, "ERROR:   and memory_replacement_policy in squid.conf!");
    fatalf("ERROR: Unknown policy %s\n", settings->type);
    return NULL;		/* NOTREACHED */
}

#if 0
void
storeSwapFileNumberSet(StoreEntry * e, sfileno filn)
{
    if (e->swap_file_number == filn)
	return;
    if (filn < 0) {
	assert(-1 == filn);
	storeDirMapBitReset(e->swap_file_number);
	storeDirLRUDelete(e);
	e->swap_file_number = -1;
    } else {
	assert(-1 == e->swap_file_number);
	storeDirMapBitSet(e->swap_file_number = filn);
	storeDirLRUAdd(e);
    }
}
#endif

/* Defer server-side reads */
void
storeDeferRead(StoreEntry * e, int fd)
{
    MemObject *mem = e->mem_obj;
    EBIT_SET(e->flags, ENTRY_DEFER_READ);
    if (fd >= 0) {
	mem->serverfd = fd;
	commDeferFD(fd);
    }
}

/* Resume reading from the server-side */
void
storeResumeRead(StoreEntry * e)
{
    MemObject *mem = e->mem_obj;
    EBIT_CLR(e->flags, ENTRY_DEFER_READ);
    if (mem->serverfd != -1) {
	commResumeFD(mem->serverfd);
	mem->serverfd = -1;
    }
}

/* Reset defer state when FD goes away under our feets */
void
storeResetDefer(StoreEntry * e)
{
    EBIT_CLR(e->flags, ENTRY_DEFER_READ);
    if (e->mem_obj)
	e->mem_obj->serverfd = -1;
}
