
/*
 * $Id: store_client.c 13958 2009-04-21 08:37:50Z adrian.chadd $
 *
 * DEBUG: section 20    Storage Manager Client-Side Interface
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

#if HTTP_GZIP
#include "HttpGzip.h"
#endif

/*!
 * @class store_client
 *
 * @abstract
 *	The Store Client module is the data pump between a data provider
 *	(say, a http server/peer connection, or the storage layer) and
 *	one or more clients requesting said data.
 *
 * @discussion
 *	This module takes care of a variety of functions - it interfaces
 *	to the disk and memory store for data retrieval; it handles
 *	reading and verifying the disk store metadata (which will fill out
 *	various fields in the MemObject structure) and it allows for
 *	multiplexing data from one source to many clients.
 *
 *	All objects are considered "in the store" whether cachable or not.
 *	Un-cachable objects are simply marked private and have a private
 *	hash key which will never be found by a subsequent request.
 *
 *	Requests are either "memory" or "disk" clients. A "memory" client
 *	is one which can be satisfied by data already in the MemObject
 *	stmem list. A "disk" client is one which requires a read from
 *	the disk store (via storeRead()) to retrieve the requested data.
 *	Note that one object may have both memory and disk clients, depending
 *	upon how much of an object is in the MemObject stmem list.
 *	Another important note here is that memory clients may become disk
 *	clients, but (TODO Check!) disk clients never "become" memory clients.
 *
 *	The lifecycle of a store client goes something like this:
 *	+ the client-side (at the moment) creates a StoreEntry for a forward
 *	  connection to throw data into for a given request, or finds an existing
 *	  StoreEntry to begin fetching data from.
 *	+ the client-side then registers a store client on said StoreEntry's MemObject
 *	+ The client-side code then calls storeClientRef() / storeClientCopyHeaders()
 *	  to start copying in data
 *	+ if the object is in memory, the data will be returned
 *	+ if the object is on disk, the data will be scheduled for read via storeRead()
 *	  when enough data is actually on the disk to be read back via.
 *	+ if the data is not in memory or on disk, the store client will wait for
 *	  data to be appended (or the object to be aborted/closed) before notifying
 *	  the caller about the available data.
 *	+ when finished, the client-side will destroy the store client via
 *	  storeClientUnregister(), which will (hopefully) abort any pending disk read.
 *
 *	The server-side code has a StoreEntry to feed data into. Each append of data
 *	via a routine or two in store.c (storeAppend(), storeAppendPrintf(), etc)
 *	can lead to InvokeHandlers() being called, which will check all the clients
 *	registered to this store client. The completion of a storeRead() call will
 *	also pass data back to the store client which queued it. Generally, the same
 *	processes which call InvokeHandlers() on a data append will also call
 *	storeSwapOut().
 *
 *	The disk read path also handles reading in and parsing the swap metadata,
 *	populating the MemObject with the relevant data from the metadata, and
 *	then parsing the HTTP reply and populating the MemObject->reply with that.
 *
 *	Finally, storeResumeRead() is called in storeClientCopy3() to resume the
 *	server-side read if said read has been suspended. This forms part of the
 *	"flow control" used to start/stop server-side reading for event based
 *	comm stuff (kqueue, epoll, devpoll, etc.)
 */

/*
 * NOTE: 'Header' refers to the swapfile metadata header.
 *       'Body' refers to the swapfile body, which is the full
 *        HTTP reply (including HTTP headers and body).
 */
static STRCB storeClientReadBody;
static STRCB storeClientReadHeader;
static void storeClientCopy2(StoreEntry * e, store_client * sc);
static void storeClientCopy3(StoreEntry * e, store_client * sc);
static void storeClientFileRead(store_client * sc);
static EVH storeClientCopyEvent;
static store_client_t storeClientType(StoreEntry *);
static int CheckQuickAbort2(StoreEntry * entry);
static void CheckQuickAbort(StoreEntry * entry);

#if STORE_CLIENT_LIST_DEBUG
static store_client *
storeClientListSearch(const MemObject * mem, void *data)
{
    dlink_node *node;
    store_client *sc = NULL;
    for (node = mem->clients.head; node; node = node->next) {
	sc = node->data;
	if (sc->owner == data)
	    return sc;
    }
    return NULL;
}
#endif

/*@!
 * @function
 *	storeClientType
 * @abstract
 *	Determine what kind of store client should be for a given StoreEntry.
 * @discussion
 *	The logic is mostly straightforward. If the object is on disk
 *	then it may be a store disk client. If the object isn't yet
 *	on disk then it can't be made a disk client (as the data may
 *	not yet be on disk, and creation of the swap file may fail.
 *
 * @param	e	StoreEntry to use when making the client type decision
 * @return	one of STORE_DISK_CLIENT, STORE_MEM_CLIENT
 */
static store_client_t
storeClientType(StoreEntry * e)
{
    MemObject *mem = e->mem_obj;
    if (mem->inmem_lo)
	return STORE_DISK_CLIENT;
    if (EBIT_TEST(e->flags, ENTRY_ABORTED)) {
	/* I don't think we should be adding clients to aborted entries */
	debugs(20, 1, "storeClientType: adding to ENTRY_ABORTED entry");
	return STORE_MEM_CLIENT;
    }
    if (e->store_status == STORE_OK) {
	if (mem->inmem_lo == 0 && mem->inmem_hi > 0)
	    return STORE_MEM_CLIENT;
	else
	    return STORE_DISK_CLIENT;
    }
    /* here and past, entry is STORE_PENDING */
    /*
     * If this is the first client, let it be the mem client
     */
    else if (mem->nclients == 1)
	return STORE_MEM_CLIENT;
    /*
     * If there is no disk file to open yet, we must make this a
     * mem client.  If we can't open the swapin file before writing
     * to the client, there is no guarantee that we will be able
     * to open it later when we really need it.
     */
    else if (e->swap_status == SWAPOUT_NONE)
	return STORE_MEM_CLIENT;
    /*
     * otherwise, make subsequent clients read from disk so they
     * can not delay the first, and vice-versa.
     */
    else
	return STORE_DISK_CLIENT;
}

CBDATA_TYPE(store_client);

/* add client with fd to client list */
/*!
 * @function
 *	storeClientRegister
 * @abstract
 *	Create a StoreClient for the given StoreEntry and owner
 * @discussion
 *	The caller must make the judgement call whether to create a
 *	new StoreClient for the given StoreEntry, or to register the
 *	StoreEntry with an existing StoreClient.
 *
 *	The store client type is determined at this point and will
 *	remain constant for the lifetime of the store client.
 *
 * @param
 *	e		StoreEntry to reigster
 *	owner		The "owner" (TODO: used for debugging, but how/when?)
 * @return		a newly created store_client
 */
store_client *
storeClientRegister(StoreEntry * e, void *owner)
{
    MemObject *mem = e->mem_obj;
    store_client *sc;
    assert(mem);
    e->refcount++;
    mem->nclients++;
    CBDATA_INIT_TYPE(store_client);
    sc = cbdataAlloc(store_client);
    sc->callback_data = NULL;
    sc->seen_offset = 0;
    sc->copy_offset = 0;
    sc->flags.disk_io_pending = 0;
    sc->entry = e;
    storeLockObject(sc->entry);
    sc->type = storeClientType(e);
#if STORE_CLIENT_LIST_DEBUG
    assert(!storeClientListSearch(mem, owner));
    sc->owner = owner;
#endif
    dlinkAdd(sc, &sc->node, &mem->clients);
#if DELAY_POOLS
    sc->delay_id = 0;
#endif
    return sc;
}

/*!
 * @function
 *	storeClientCallback
 * @abstract
 *	Schedule a callback with the given size to the registered client callback
 * @discussion
 *	Since the called code may initiate another callback, the current callback
 *	details are first NULLed out before hand.
 *
 *	Call the callback with minimum based on size and copysize, just in case
 *	the calling code doesn't handle being given a larger buffer.
 *
 *	This routine unlocks the callback data supplied in storeClientRef().
 *
 * @param	sc		store client to notify
 * @param	sz		size to return, -1 on error, 0 on EOF
 */
static void
storeClientCallback(store_client * sc, ssize_t sz)
{
    STNCB *new_callback = sc->new_callback;
    void *cbdata = sc->callback_data;
    mem_node_ref nr;

    assert(sc->new_callback);
    sc->new_callback = NULL;
    sc->callback_data = NULL;
    nr = sc->node_ref;		/* XXX this should be a reference; and we should dereference our copy! */
    /* This code "transfers" its ownership (and reference) of the node_ref to the caller. Ugly, but works. */
    sc->node_ref.node = NULL;
    sc->node_ref.offset = -1;
    /* Can't use XMIN here - sz is signed; copy_size isn't; things get messy */
    if (sz < 0)
	new_callback(cbdata, nr, -1);
    else
	new_callback(cbdata, nr, XMIN(sz, sc->copy_size));
    cbdataUnlock(cbdata);
}

/*!
 * @function
 *	storeClientCopyEvent
 *
 * @abstract
 *	A timed event callback handler; re-attempt the next client copy
 *
 * @discussion
 *	I'm not sure why sc->new_callback would be NULL here; I guess this
 *	function is attempting to handle the situation where the store client
 *	has subsequently had the callback occur for whatever reason and
 *	this event hasn't been removed.
 *
 *	The event isn't actually explicitly ever removed - if the store client
 *	is closed (via a call to storeClientUnregister()) then the store client
 *	pointer will become invalid and the event won't be called.
 *	
 * @param	data	generic pointer containing the store client to check
 */
static void
storeClientCopyEvent(void *data)
{
    store_client *sc = data;
    debugs(20, 3, "storeClientCopyEvent: Running");
    sc->flags.copy_event_pending = 0;
    if (!sc->new_callback)
	return;
    storeClientCopy2(sc->entry, sc);
}

/*!
 * @function
 *	storeClientRef
 * @abstract
 *	Request some data from a backing object
 * @discussion
 *	Object data is returned when the object has data available past "seen_offset"
 *	but is returned from "copy_offset". This is to faciliate handling requests
 *	with response HTTP headers that span the given "size".
 *
 *	This routine started life as "storeClientCopy()" and would copy the data
 *	into a given buffer. Today, it returns a reference to an stmem page, including
 *	the starting offset (to satisfy copy_offset) and a length. The returned length
 *	MAY be longer than "size" so calling code should be prepared for that.
 *
 *	The callback is only performed if "data" is still valid (via cbdataValid()).
 *
 * @param	store_client		The store client to request data from
 * @param	e			The StoreEntry to request data from
 * @param	seen_offset		How much data has already been "seen" by the client
 * @param	copy_offset		Where to begin returning data from
 * @param	size			The maximum(!) amount of data requested
 * @param	callback		Callback to invoke on completion of this request
 * @param	data			Callback data; must be a "cbdata" type
 */
void
storeClientRef(store_client * sc,
    StoreEntry * e,
    squid_off_t seen_offset,
    squid_off_t copy_offset,
    size_t size,
    STNCB * callback,
    void *data)
{
    debugs(20, 3, "storeClientRef: %s, seen %" PRINTF_OFF_T ", want %" PRINTF_OFF_T ", size %d, cb %p, cbdata %p",
	storeKeyText(e->hash.key),
	seen_offset,
	copy_offset,
	(int) size,
	callback,
	data);
    assert(sc != NULL);
#if STORE_CLIENT_LIST_DEBUG
    assert(sc == storeClientListSearch(e->mem_obj, data));
#endif
    assert(sc->new_callback == NULL);
    assert(sc->entry == e);
    sc->seen_offset = seen_offset;
    sc->new_callback = callback;
    sc->callback_data = data;
    cbdataLock(sc->callback_data);
    sc->copy_size = size;
    sc->copy_offset = copy_offset;
    /* If the read is being deferred, run swapout in case this client has the 
     * lowest seen_offset. storeSwapOut() frees the memory and clears the 
     * ENTRY_DEFER_READ bit if necessary */
    if (EBIT_TEST(e->flags, ENTRY_DEFER_READ)) {
	storeSwapOut(e);
    }
    storeClientCopy2(e, sc);
}

/*
 * This function is used below to decide if we have any more data to
 * send to the client.  If the store_status is STORE_PENDING, then we
 * do have more data to send.  If its STORE_OK, then
 * we continue checking.  If the object length is negative, then we
 * don't know the real length and must open the swap file to find out.
 * If the length is >= 0, then we compare it to the requested copy
 * offset.
 */
static int
storeClientNoMoreToSend(StoreEntry * e, store_client * sc)
{
    squid_off_t len;
    if (e->store_status == STORE_PENDING)
	return 0;
    if ((len = objectLen(e)) < 0)
	return 0;
    if (sc->copy_offset < len)
	return 0;
    return 1;
}

/*!
 * @function
 *	storeClientCopy2
 *
 * @abstract
 *	Check whether the copy should occur, and attempt it if so. Schedule
 *	a later attempt at copying if one is in progress.
 *
 * @discussion
 *	This routine primarily exists to prevent re-entry into the store client
 *	copy routine(s). The copy only takes place if another copy on this store
 *	client is not. It will be re-attempted later after the next call to
 *	InvokeHandlers().
 *
 *	If a copy event is already occuring (as in, higher up in the call stack)
 *	then an event is registered to immediately call the next time through the
 *	event loop. This is a slightly hacky use of the event code which isn't
 *	really intended for this to happen at any large amounts.
 *
 *	It is called from storeClientRef(), storeClientCopyEvent(), InvokeHandlers().
 *
 * @param	e	StoreEntry the client belongs to
 * @param	sc	store client to check whether data is available for
 */
static void
storeClientCopy2(StoreEntry * e, store_client * sc)
{
    if (sc->flags.copy_event_pending)
	return;
    if (EBIT_TEST(e->flags, ENTRY_FWD_HDR_WAIT)) {
	debugs(20, 5, "storeClientCopy2: returning because ENTRY_FWD_HDR_WAIT set");
	return;
    }
    if (sc->flags.store_copying) {
	sc->flags.copy_event_pending = 1;
	debugs(20, 3, "storeClientCopy2: Queueing storeClientCopyEvent()");
	eventAdd("storeClientCopyEvent", storeClientCopyEvent, sc, 0.0, 0);
	return;
    }
    cbdataLock(sc);		/* ick, prevent sc from getting freed */
    sc->flags.store_copying = 1;
    debugs(20, 3, "storeClientCopy2: %s", storeKeyText(e->hash.key));
    assert(sc->new_callback);
    /*
     * We used to check for ENTRY_ABORTED here.  But there were some
     * problems.  For example, we might have a slow client (or two) and
     * the server-side is reading far ahead and swapping to disk.  Even
     * if the server-side aborts, we want to give the client(s)
     * everything we got before the abort condition occurred.
     */
    storeClientCopy3(e, sc);
    sc->flags.store_copying = 0;
    cbdataUnlock(sc);		/* ick, allow sc to be freed */
}

/*!
 * @function
 *	storeClientCopy3
 *
 * @abstract
 *	Attempt a copy, schedule a read or copy event where needed.
 *
 * @discussion
 *	XXX - TODO!
 *
 * @param	e	StoreEntry to copy from
 * @param	sc	Store Client to copy to
 */
static void
storeClientCopy3(StoreEntry * e, store_client * sc)
{
    MemObject *mem = e->mem_obj;
    ssize_t sz = -1;

    if (storeClientNoMoreToSend(e, sc)) {
	/* There is no more to send! */
	storeClientCallback(sc, 0);
	return;
    }
    if (e->store_status == STORE_PENDING && sc->seen_offset >= mem->inmem_hi) {
	/* client has already seen this, wait for more */
	debugs(20, 3, "storeClientCopy3: Waiting for more");

	/* If the read is backed off and all clients have seen all the data in
	 * memory, re-poll the fd */
	if ((EBIT_TEST(e->flags, ENTRY_DEFER_READ)) &&
	    (storeLowestMemReaderOffset(e) == mem->inmem_hi)) {
	    debugs(20, 3, "storeClientCopy3: %s - clearing ENTRY_DEFER_READ", e->mem_obj->url);
	    /* Clear the flag and re-poll the fd */
	    storeResumeRead(e);
	}
	return;
    }
    /*
     * Slight weirdness here.  We open a swapin file for any
     * STORE_DISK_CLIENT, even if we can copy the requested chunk
     * from memory in the next block.  We must try to open the
     * swapin file before sending any data to the client side.  If
     * we postpone the open, and then can not open the file later
     * on, the client loses big time.  Its transfer just gets cut
     * off.  Better to open it early (while the client side handler
     * is clientCacheHit) so that we can fall back to a cache miss
     * if needed.
     */
    if (STORE_DISK_CLIENT == sc->type && NULL == sc->swapin_sio) {
	debugs(20, 3, "storeClientCopy3: Need to open swap in file");
	/* gotta open the swapin file */
	if (storeTooManyDiskFilesOpen()) {
	    /* yuck -- this causes a TCP_SWAPFAIL_MISS on the client side */
	    storeClientCallback(sc, -1);
	    return;
	} else if (!sc->flags.disk_io_pending) {
	    /* Don't set store_io_pending here */
	    storeSwapInStart(sc);
	    if (NULL == sc->swapin_sio) {
		storeClientCallback(sc, -1);
		return;
	    }
	    /*
	     * If the open succeeds we either copy from memory, or
	     * schedule a disk read in the next block.
	     */
	} else {
	    debugs(20, 1, "WARNING: Averted multiple fd operation (1)");
	    return;
	}
    }
    if (sc->copy_offset >= mem->inmem_lo && sc->copy_offset < mem->inmem_hi) {
	/* What the client wants is in memory */
	debugs(20, 3, "storeClientCopy3: Copying from memory");
	assert(sc->new_callback);
	assert(sc->node_ref.node == NULL);	/* We should never, ever have a node here; or we'd leak! */
	sz = stmemRef(&mem->data_hdr, sc->copy_offset, &sc->node_ref);
	if (EBIT_TEST(e->flags, RELEASE_REQUEST))
	    storeSwapOutMaintainMemObject(e);
	storeClientCallback(sc, sz);
	return;
    }
    /* What the client wants is not in memory. Schedule a disk read */
    assert(STORE_DISK_CLIENT == sc->type);
    assert(!sc->flags.disk_io_pending);
    debugs(20, 3, "storeClientCopy3: reading from STORE");
    /* Just in case there's a node here; free it */
    stmemNodeUnref(&sc->node_ref);
    storeClientFileRead(sc);
}

static void
storeClientFileRead(store_client * sc)
{
    MemObject *mem = sc->entry->mem_obj;
    assert(sc->new_callback);
    assert(!sc->flags.disk_io_pending);
    sc->flags.disk_io_pending = 1;
    assert(sc->node_ref.node == NULL);	/* We should never, ever have a node here; or we'd leak! */
    stmemNodeRefCreate(&sc->node_ref);	/* Creates an entry with reference count == 1 */
    if (mem->swap_hdr_sz == 0) {
	storeRead(sc->swapin_sio,
	    sc->node_ref.node->data,
	    XMIN(SM_PAGE_SIZE, sc->copy_size),
	    0,
	    storeClientReadHeader,
	    sc);
    } else {
	if (sc->entry->swap_status == SWAPOUT_WRITING)
	    assert(storeSwapOutObjectBytesOnDisk(mem) > sc->copy_offset);	/* XXX is this right? Shouldn't we incl. mem->swap_hdr_sz? */
	storeRead(sc->swapin_sio,
	    sc->node_ref.node->data,
	    XMIN(SM_PAGE_SIZE, sc->copy_size),
	    sc->copy_offset + mem->swap_hdr_sz,
	    storeClientReadBody,
	    sc);
    }
}

/*
 * Try to parse the header.
 * return -1 on error, 0 on more required, +1 on completed.
 */
static int
storeClientParseHeader(store_client * sc, const char *b, int l)
{
    if (sc->copy_offset == 0 && l > 0 && memHaveHeaders(sc->entry->mem_obj) == 0)
	return httpReplyParse(sc->entry->mem_obj->reply, b, headersEnd(b, l));
    else
	return 1;
}

static void
storeClientReadBody(void *data, const char *buf_unused, ssize_t len)
{
    char *cbuf = NULL;
    store_client *sc = data;
    assert(sc->flags.disk_io_pending);

    sc->flags.disk_io_pending = 0;
    assert(sc->new_callback);
    assert(sc->node_ref.node);
    cbuf = sc->node_ref.node->data;
    /* XXX update how much data in that mem page is active; argh this should be done in a storage layer */
    sc->node_ref.node->len = len;
    debugs(20, 3, "storeClientReadBody: len %d", (int) len);
    (void) storeClientParseHeader(sc, cbuf, len);
    storeClientCallback(sc, len);
}

static void
storeClientReadHeader(void *data, const char *buf_unused, ssize_t len)
{
    static int md5_mismatches = 0;
    store_client *sc = data;
    StoreEntry *e = sc->entry;
    MemObject *mem = e->mem_obj;
    int swap_hdr_sz = 0;
    size_t body_sz;
    size_t copy_sz;
    tlv *tlv_list;
    tlv *t;
    char *cbuf;
    int swap_object_ok = 1;
    char *new_url = NULL;
    char *new_store_url = NULL;
    assert(sc->flags.disk_io_pending);
    sc->flags.disk_io_pending = 0;
    assert(sc->new_callback);
    assert(sc->node_ref.node);
    cbuf = sc->node_ref.node->data;
    debugs(20, 3, "storeClientReadHeader: len %d", (int) len);
    /* XXX update how much data in that mem page is active; argh this should be done in a storage layer */
    sc->node_ref.node->len = len;
    if (len < 0) {
	debugs(20, 3, "storeClientReadHeader: %s", xstrerror());
	storeClientCallback(sc, len);
	return;
    }
    assert(len <= SM_PAGE_SIZE);
    tlv_list = storeSwapMetaUnpack(cbuf, &swap_hdr_sz);
    if (swap_hdr_sz > len) {
	/* oops, bad disk file? */
	debugs(20, 1, "WARNING: swapfile header too small");
	storeClientCallback(sc, -1);
	return;
    }
    if (tlv_list == NULL) {
	debugs(20, 1, "WARNING: failed to unpack meta data");
	storeClientCallback(sc, -1);
	return;
    }
    /*
     * Check the meta data and make sure we got the right object.
     */
    for (t = tlv_list; t && swap_object_ok; t = t->next) {
	switch (t->type) {
	case STORE_META_KEY:
	    assert(t->length == SQUID_MD5_DIGEST_LENGTH);
	    if (!EBIT_TEST(e->flags, KEY_PRIVATE) &&
		memcmp(t->value, e->hash.key, SQUID_MD5_DIGEST_LENGTH)) {
		debugs(20, 2, "storeClientReadHeader: swapin MD5 mismatch");
		debugs(20, 2, "\t%s", storeKeyText(t->value));
		debugs(20, 2, "\t%s", storeKeyText(e->hash.key));
		if (isPowTen(++md5_mismatches))
		    debugs(20, 1, "WARNING: %d swapin MD5 mismatches",
			md5_mismatches);
		swap_object_ok = 0;
	    }
	    break;
	case STORE_META_URL:
	    new_url = xstrdup(t->value);
	    break;
	case STORE_META_STOREURL:
	    new_store_url = xstrdup(t->value);
	    break;
	case STORE_META_OBJSIZE:
	    break;
	case STORE_META_STD:
	case STORE_META_STD_LFS:
	    break;
#if HTTP_GZIP
	case STORE_META_GZIP:

	    debugs(20, 2, "Got STORE_META_GZIP: %s", (char *)t->value);

	    if (strcmp("gzip", t->value) == 0) {
		e->compression_type = SQUID_CACHE_GZIP;
	    }
	    else if (strcmp("deflate", t->value) == 0) {
		e->compression_type = SQUID_CACHE_DEFLATE;
	    }

	    break;
#endif
	case STORE_META_VARY_HEADERS:
	    if (mem->vary_headers) {
		if (strcmp(mem->vary_headers, t->value) != 0)
		    swap_object_ok = 0;
	    } else {
		/* Assume the object is OK.. remember the vary request headers */
		mem->vary_headers = xstrdup(t->value);
	    }
	    break;
	default:
	    debugs(20, 2, "WARNING: got unused STORE_META type %d", t->type);
	    break;
	}
    }

    /* Check url / store_url */
    do {
	if (new_url == NULL) {
	    debugs(20, 1, "storeClientReadHeader: no URL!");
	    swap_object_ok = 0;
	    break;
	}
	/*
	 * If we have a store URL then it must match the requested object URL.
	 * The theory is that objects with a store URL have been normalised
	 * and thus a direct access which didn't go via the rewrite framework
	 * are illegal!
	 */
	if (new_store_url) {
	    if (NULL == mem->store_url)
		mem->store_url = new_store_url;
	    else if (0 == strcasecmp(mem->store_url, new_store_url))
		(void) 0;	/* a match! */
	    else {
		debugs(20, 1, "storeClientReadHeader: store URL mismatch");
		debugs(20, 1, "\t{%s} != {%s}", (char *) new_store_url, mem->store_url);
		swap_object_ok = 0;
		break;
	    }
	}
	/* If we have no store URL then the request and the memory URL must match */
	if ((!new_store_url) && mem->url && strcasecmp(mem->url, new_url) != 0) {
	    debugs(20, 1, "storeClientReadHeader: URL mismatch");
	    debugs(20, 1, "\t{%s} != {%s}", (char *) new_url, mem->url);
	    swap_object_ok = 0;
	    break;
	}
    } while (0);

    tlv_free(tlv_list);
    xfree(new_url);
    /* don't free new_store_url if its owned by the mem object now */
    if (mem->store_url != new_store_url)
	xfree(new_store_url);

    if (!swap_object_ok) {
	storeClientCallback(sc, -1);
	return;
    }
    mem->swap_hdr_sz = swap_hdr_sz;
    mem->object_sz = e->swap_file_sz - swap_hdr_sz;
    /*
     * If our last read got some data the client wants, then give
     * it to them, otherwise schedule another read.
     */
    body_sz = len - swap_hdr_sz;
    if (sc->copy_offset < body_sz) {
	/*
	 * we have (part of) what they want
	 */
	copy_sz = XMIN(sc->copy_size, body_sz);
	debugs(20, 3, "storeClientReadHeader: copying %d bytes of body",
	    (int) copy_sz);
	debugs(20, 8, "sc %p; node_ref->node %p; data %p; copy size %d; data size %d",
	    sc, sc->node_ref.node, sc->node_ref.node->data, (int) copy_sz, (int) len);
	xmemmove(cbuf, cbuf + swap_hdr_sz, copy_sz);
	if (sc->copy_offset == 0 && len > 0 && memHaveHeaders(mem) == 0)
	    (void) storeClientParseHeader(sc, cbuf, copy_sz);

#if HTTP_GZIP
	if (e->compression_type) {
	    httpGzipClearHeaders(mem->reply, e->compression_type);
	}

	if (mem->vary_headers) {
	    httpHeaderDelById(&mem->reply->header, HDR_ETAG);
	}
#endif
	
	storeClientCallback(sc, copy_sz);
	return;
    }
    /*
     * we don't have what the client wants, but at least we now
     * know the swap header size.
     */
    /* Just in case there's a node here; free it */
    stmemNodeUnref(&sc->node_ref);
    storeClientFileRead(sc);
}

int
storeClientCopyPending(store_client * sc, StoreEntry * e, void *data)
{
#if STORE_CLIENT_LIST_DEBUG
    assert(sc == storeClientListSearch(e->mem_obj, data));
#endif
    assert(sc->entry == e);
    if (sc == NULL)
	return 0;
    if (sc->new_callback == NULL)
	return 0;
    return 1;
}

/*!
 * @function
 *	storeClientUnregister
 * @abstract
 *	Free the given store_client
 * @discussion
 *	This routine is quite messy. If sc is NULL, it returns immediately.
 *	If the MemObject attached to e (e->mem_obj) has no registered clients
 *	then it returns immediately. These two conditions should be assert()'ions
 *	and calling code should be fixed to only unregister where appropriate.
 *
 *	TODO: fully explore and document what can actually happen here!
 *
 * @param	sc	store_client to unregister
 * @param	e	StoreEntry to unregister
 * @param	owner	Original owner of store_client; used for debugging
 */
int
storeClientUnregister(store_client * sc, StoreEntry * e, void *owner)
{
    MemObject *mem = e->mem_obj;
    if (sc == NULL)
	return 0;
    debugs(20, 3, "storeClientUnregister: called for '%s'", storeKeyText(e->hash.key));
#if STORE_CLIENT_LIST_DEBUG
    assert(sc == storeClientListSearch(e->mem_obj, owner));
#endif
    assert(sc->entry == e);
    if (mem->clients.head == NULL)
	return 0;
    dlinkDelete(&sc->node, &mem->clients);
    mem->nclients--;
    if (e->store_status == STORE_OK && e->swap_status != SWAPOUT_DONE)
	storeSwapOut(e);
    if (sc->swapin_sio) {
	storeClose(sc->swapin_sio);
	cbdataUnlock(sc->swapin_sio);
	sc->swapin_sio = NULL;
	statCounter.swap.ins++;
    }
    if (NULL != sc->new_callback) {
	/* callback with ssize = -1 to indicate unexpected termination */
	debugs(20, 3, "storeClientUnregister: store_client for %s has a callback",
	    mem->url);
	storeClientCallback(sc, -1);
    }
    stmemNodeUnref(&sc->node_ref);
#if DELAY_POOLS
    delayUnregisterDelayIdPtr(&sc->delay_id);
#endif
    storeSwapOutMaintainMemObject(e);
    if (mem->nclients == 0)
	CheckQuickAbort(e);
    storeUnlockObject(sc->entry);
    sc->entry = NULL;
    cbdataFree(sc);
    return 1;
}

squid_off_t
storeLowestMemReaderOffset(const StoreEntry * entry)
{
    const MemObject *mem = entry->mem_obj;
    squid_off_t lowest = mem->inmem_hi + 1;
    squid_off_t highest = -1;
    store_client *sc;
    dlink_node *nx = NULL;
    dlink_node *node;

    for (node = mem->clients.head; node; node = nx) {
	sc = node->data;
	nx = node->next;
	if (sc->copy_offset > highest)
	    highest = sc->copy_offset;
	if (mem->swapout.sio != NULL && sc->type != STORE_MEM_CLIENT)
	    continue;
	if (sc->copy_offset < lowest)
	    lowest = sc->copy_offset;
    }
    if (highest < lowest && highest >= 0)
	return highest;
    return lowest;
}

/* Call handlers waiting for  data to be appended to E. */
void
InvokeHandlers(StoreEntry * e)
{
    int i = 0;
    MemObject *mem = e->mem_obj;
    store_client *sc;
    dlink_node *nx = NULL;
    dlink_node *node;

    debugs(20, 3, "InvokeHandlers: %s", storeKeyText(e->hash.key));
    /* walk the entire list looking for valid callbacks */
    for (node = mem->clients.head; node; node = nx) {
	sc = node->data;
	nx = node->next;
	debugs(20, 3, "InvokeHandlers: checking client #%d", i++);
	if (sc->new_callback == NULL)
	    continue;
	if (sc->flags.disk_io_pending)
	    continue;
	storeClientCopy2(e, sc);
    }
}

int
storePendingNClients(const StoreEntry * e)
{
    MemObject *mem = e->mem_obj;
    int npend = NULL == mem ? 0 : mem->nclients;
    debugs(20, 3, "storePendingNClients: returning %d", npend);
    return npend;
}

/* return 1 if the request should be aborted */
static int
CheckQuickAbort2(StoreEntry * entry)
{
    squid_off_t curlen;
    squid_off_t minlen;
    squid_off_t expectlen;
    MemObject *mem = entry->mem_obj;
    assert(mem);
    debugs(20, 3, "CheckQuickAbort2: entry=%p, mem=%p", entry, mem);
    if (mem->request && !mem->request->flags.cachable) {
	debugs(20, 3, "CheckQuickAbort2: YES !mem->request->flags.cachable");
	return 1;
    }
    if (EBIT_TEST(entry->flags, KEY_PRIVATE)) {
	debugs(20, 3, "CheckQuickAbort2: YES KEY_PRIVATE");
	return 1;
    }
    expectlen = mem->reply->content_length + mem->reply->hdr_sz;
    curlen = mem->inmem_hi;
    minlen = Config.quickAbort.min << 10;
    if (minlen < 0) {
	debugs(20, 3, "CheckQuickAbort2: NO disabled");
	return 0;
    }
    if (curlen > expectlen) {
	debugs(20, 3, "CheckQuickAbort2: YES bad content length");
	return 1;
    }
    if ((expectlen - curlen) < minlen) {
	debugs(20, 3, "CheckQuickAbort2: NO only little more left");
	return 0;
    }
    if ((expectlen - curlen) > (Config.quickAbort.max << 10)) {
	debugs(20, 3, "CheckQuickAbort2: YES too much left to go");
	return 1;
    }
    if (expectlen < 100) {
	debugs(20, 3, "CheckQuickAbort2: NO avoid FPE");
	return 0;
    }
    if ((curlen / (expectlen / 100)) > Config.quickAbort.pct) {
	debugs(20, 3, "CheckQuickAbort2: NO past point of no return");
	return 0;
    }
    debugs(20, 3, "CheckQuickAbort2: YES default, returning 1");
    return 1;
}

static void
CheckQuickAbort(StoreEntry * entry)
{
    if (entry == NULL)
	return;
    if (storePendingNClients(entry) > 0)
	return;
    if (entry->store_status != STORE_PENDING)
	return;
    if (EBIT_TEST(entry->flags, ENTRY_SPECIAL))
	return;
    if (CheckQuickAbort2(entry) == 0)
	return;
    statCounter.aborted_requests++;
    storeAbort(entry);
}

static void
storeClientCopyHeadersCB(void *data, mem_node_ref nr, ssize_t size)
{
    store_client *sc = data;
    STHCB *cb = sc->header_callback;
    void *cbdata = sc->header_cbdata;

    assert(cb);
    assert(cbdata);

    /* Leave these in for now, just for debugging */
#if 0
    sc->header_callback = NULL;
    sc->header_cbdata = NULL;
#endif

    stmemNodeUnref(&nr);
    /* XXX should cbdata lock/unlock the cbdata? */
    if (size < 0 || !memHaveHeaders(sc->entry->mem_obj)) {
	cb(cbdata, NULL);
	return;
    }
    cb(cbdata, sc->entry->mem_obj->reply);
}

/*
 * This is the eventual API which store clients should use to fetch the headers.
 */
void
storeClientCopyHeaders(store_client * sc, StoreEntry * e, STHCB * callback, void *callback_data)
{
    sc->header_callback = callback;
    sc->header_cbdata = callback_data;

    /* This kicks off either the memory read, waiting for the data to appear, or the disk read */
    storeClientRef(sc, e, 0, 0, SM_PAGE_SIZE, storeClientCopyHeadersCB, sc);
}
