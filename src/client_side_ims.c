#include "squid.h"

#include "client_side.h"
#include "client_side_ims.h"


static void clientHandleIMSReply(void *data, HttpReply * rep);

int
modifiedSince(StoreEntry * entry, request_t * request)
{
    squid_off_t object_length;
    MemObject *mem = entry->mem_obj;
    time_t mod_time = entry->lastmod;
    debug(33, 3) ("modifiedSince: '%s'\n", storeLookupUrl(entry));
    debug(33, 3) ("modifiedSince: mod_time = %ld\n", (long int) mod_time);
    if (mod_time < 0)
        return 1;
    /* Find size of the object */
    object_length = mem->reply->content_length;
    if (object_length < 0)
        object_length = contentLen(entry);
    if (mod_time > request->ims) {
        debug(33, 3) ("--> YES: entry newer than client\n");
        return 1;
    } else if (mod_time < request->ims) {
        debug(33, 3) ("-->  NO: entry older than client\n");
        return 0;
    } else if (request->imslen < 0) {
        debug(33, 3) ("-->  NO: same LMT, no client length\n");
        return 0;
    } else if (request->imslen == object_length) {
        debug(33, 3) ("-->  NO: same LMT, same length\n");
        return 0;
    } else {
        debug(33, 3) ("--> YES: same LMT, different length\n");
        return 1;
    }  
}

void
clientProcessExpired(clientHttpRequest * http)
{
    char *url = http->uri;
    StoreEntry *entry = NULL;
    int hit = 0;
    const char *etag;
    const int can_revalidate = http->entry->mem_obj->reply->sline.status == HTTP_OK;
    debug(33, 3) ("clientProcessExpired: '%s'\n", http->uri);
    /*
     * check if we are allowed to contact other servers
     * @?@: Instead of a 504 (Gateway Timeout) reply, we may want to return 
     *      a stale entry *if* it matches client requirements
     */
    if (clientOnlyIfCached(http)) {
	clientProcessOnlyIfCachedMiss(http);
	return;
    }
    http->request->flags.refresh = 1;
    http->old_entry = http->entry;
    http->old_sc = http->sc;
    if (http->entry->mem_obj && http->entry->mem_obj->ims_entry) {
	entry = http->entry->mem_obj->ims_entry;
	debug(33, 5) ("clientProcessExpired: collapsed request\n");
	if (EBIT_TEST(entry->flags, ENTRY_ABORTED)) {
	    debug(33, 1) ("clientProcessExpired: collapsed request ABORTED!\n");
	    entry = NULL;
	} else if (http->entry->mem_obj->refresh_timestamp + Config.collapsed_forwarding_timeout < squid_curtime) {
	    debug(33, 1) ("clientProcessExpired: collapsed request STALE!\n");
	    entry = NULL;
	}
	if (entry) {
	    http->request->flags.collapsed = 1;		/* Don't trust the store entry */
	    storeLockObject(entry);
	    hit = 1;
	} else {
	    storeUnlockObject(http->entry->mem_obj->ims_entry);
	    http->entry->mem_obj->ims_entry = NULL;
	}
    }
    if (!entry) {
	entry = storeCreateEntry(url, http->request->flags, http->request->method);
	if (http->request->store_url)
	    storeEntrySetStoreUrl(entry, http->request->store_url);
	if (http->entry->mem_obj) {
	    http->entry->mem_obj->refresh_timestamp = squid_curtime;
	    if (Config.onoff.collapsed_forwarding) {
		http->entry->mem_obj->ims_entry = entry;
		storeLockObject(http->entry->mem_obj->ims_entry);
	    }
	}
    }
    if (entry->mem_obj->old_entry) {
	storeUnlockObject(entry->mem_obj->old_entry);
	entry->mem_obj->old_entry = NULL;
    }
    entry->mem_obj->old_entry = http->old_entry;
    storeLockObject(entry->mem_obj->old_entry);
    http->sc = storeClientRegister(entry, http);
#if DELAY_POOLS
    /* delay_id is already set on original store client */
    delaySetStoreClient(http->sc, delayClient(http));
#endif
    if (can_revalidate && http->old_entry->lastmod > 0) {
	http->request->lastmod = http->old_entry->lastmod;
	http->request->flags.cache_validation = 1;
    } else
	http->request->lastmod = -1;
    debug(33, 5) ("clientProcessExpired: lastmod %ld\n", (long int) entry->lastmod);
    /* NOTE, don't call storeLockObject(), storeCreateEntry() does it */
    http->entry = entry;
    http->out.offset = 0;
    if (can_revalidate) {
	etag = httpHeaderGetStr(&http->old_entry->mem_obj->reply->header, HDR_ETAG);
	if (etag) {
	    http->request->etag = xstrdup(etag);
	    http->request->flags.cache_validation = 1;
	}
    }
    if (!hit)
	fwdStart(http->conn->fd, http->entry, http->request);
    /* Register with storage manager to receive updates when data comes in. */
    if (EBIT_TEST(entry->flags, ENTRY_ABORTED))
	debug(33, 0) ("clientProcessExpired: found ENTRY_ABORTED object\n");
    storeClientCopyHeaders(http->sc, entry,
	clientHandleIMSReply,
	http);
}

static int
clientGetsOldEntry(StoreEntry * new_entry, StoreEntry * old_entry, request_t * request)
{
    const http_status status = new_entry->mem_obj->reply->sline.status;
    if (0 == status) {
	debug(33, 5) ("clientGetsOldEntry: YES, broken HTTP reply\n");
	return 1;
    }
    /* If the reply is a failure then send the old object as a last
     * resort */
    if (status >= 500 && status < 600) {
	if (EBIT_TEST(new_entry->flags, ENTRY_NEGCACHED)) {
	    debug(33, 3) ("clientGetsOldEntry: NO, negatively cached failure reply=%d\n", status);
	    return 0;
	}
	if (refreshCheckStaleOK(old_entry, request)) {
	    debug(33, 3) ("clientGetsOldEntry: YES, failure reply=%d and old acceptable to send\n", status);
	    return 1;
	}
	debug(33, 3) ("clientGetsOldEntry: NO, failure reply=%d and old NOT acceptable to send\n", status);
	return 0;
    }
    /* If the reply is not to a cache validation conditional then
     * we should forward it to the client */
    if (!request->flags.cache_validation) {
	debug(33, 5) ("clientGetsOldEntry: NO, not a cache validation\n");
	return 0;
    }
    /* If the reply is anything but "Not Modified" then
     * we must forward it to the client */
    if (HTTP_NOT_MODIFIED != status) {
	debug(33, 5) ("clientGetsOldEntry: NO, reply=%d\n", status);
	return 0;
    }
    /* If the ETag matches the clients If-None-Match, then return
     * the servers 304 reply
     */
    if (httpHeaderHas(&new_entry->mem_obj->reply->header, HDR_ETAG) &&
	httpHeaderHas(&request->header, HDR_IF_NONE_MATCH)) {
	const char *etag = httpHeaderGetStr(&new_entry->mem_obj->reply->header, HDR_ETAG);
	String etags = httpHeaderGetList(&request->header, HDR_IF_NONE_MATCH);
	int etag_match = strListIsMember(&etags, etag, ',');
	stringClean(&etags);
	if (etag_match) {
	    debug(33, 5) ("clientGetsOldEntry: NO, client If-None-Match\n");
	    return 0;
	}
    }
    /* If the client did not send IMS in the request, then it
     * must get the old object, not this "Not Modified" reply */
    if (!request->flags.ims) {
	debug(33, 5) ("clientGetsOldEntry: YES, no client IMS\n");
	return 1;
    }
    /* If the client IMS time is prior to the entry LASTMOD time we
     * need to send the old object */
    if (modifiedSince(old_entry, request)) {
	debug(33, 5) ("clientGetsOldEntry: YES, modified since %ld\n",
	    (long int) request->ims);
	return 1;
    }
    debug(33, 5) ("clientGetsOldEntry: NO, new one is fine\n");
    return 0;
}

static void
clientHandleIMSReply(void *data, HttpReply * rep)
{
    clientHttpRequest *http = data;
    StoreEntry *entry = http->entry;
    MemObject *mem;
    const char *url = storeUrl(entry);
    int unlink_request = 0;
    StoreEntry *oldentry;
    int recopy = 1;
    debug(33, 3) ("clientHandleIMSReply: %s\n", url);
    if (http->old_entry && http->old_entry->mem_obj && http->old_entry->mem_obj->ims_entry) {
	storeUnlockObject(http->old_entry->mem_obj->ims_entry);
	http->old_entry->mem_obj->ims_entry = NULL;
    }
    if (entry == NULL) {
	return;
    }
    if (entry->mem_obj->old_entry) {
	storeUnlockObject(entry->mem_obj->old_entry);
	entry->mem_obj->old_entry = NULL;
    }
    mem = entry->mem_obj;
    if (!rep) {
	debug(33, 3) ("clientHandleIMSReply: ABORTED '%s'\n", url);
	/* We have an existing entry, but failed to validate it */
	/* Its okay to send the old one anyway */
	http->log_type = LOG_TCP_REFRESH_FAIL_HIT;
	storeClientUnregister(http->sc, entry, http);
	storeUnlockObject(entry);
	entry = http->entry = http->old_entry;
	http->sc = http->old_sc;
    } else if (clientGetsOldEntry(entry, http->old_entry, http->request)) {
	/* We initiated the IMS request, the client is not expecting
	 * 304, so put the good one back.  First, make sure the old entry
	 * headers have been loaded from disk. */
	oldentry = http->old_entry;
	if (oldentry->mem_obj->request == NULL) {
	    oldentry->mem_obj->request = requestLink(mem->request);
	    unlink_request = 1;
	}
	if (rep->sline.status == HTTP_NOT_MODIFIED) {
	    /* Don't memcpy() the whole reply structure here.  For example,
	     * www.thegist.com (Netscape/1.13) returns a content-length for
	     * 304's which seems to be the length of the 304 HEADERS!!! and
	     * not the body they refer to.  */
	    httpReplyUpdateOnNotModified(oldentry->mem_obj->reply, rep);
	    storeTimestampsSet(oldentry);
	    storeUpdate(oldentry, http->request);
	    http->log_type = LOG_TCP_REFRESH_HIT;
	} else {
	    http->log_type = LOG_TCP_REFRESH_FAIL_HIT;
	}
	storeClientUnregister(http->sc, entry, http);
	http->sc = http->old_sc;
	storeUnlockObject(entry);
	entry = http->entry = oldentry;
	if (unlink_request) {
	    requestUnlink(entry->mem_obj->request);
	    entry->mem_obj->request = NULL;
	}
    } else {
	/* the client can handle this reply, whatever it is */
	http->flags.hit = 0;
	http->log_type = LOG_TCP_REFRESH_MISS;
	if (HTTP_NOT_MODIFIED == rep->sline.status && http->request->flags.cache_validation) {
	    httpReplyUpdateOnNotModified(http->old_entry->mem_obj->reply,
		rep);
	    storeTimestampsSet(http->old_entry);
	    storeUpdate(http->old_entry, http->request);
	    if (!EBIT_TEST(http->old_entry->flags, REFRESH_FAILURE))
		http->log_type = LOG_TCP_REFRESH_HIT;
	    else
		http->log_type = LOG_TCP_REFRESH_FAIL_HIT;
	}
	/* Get rid of the old entry if not a cache validation */
	if (!http->request->flags.cache_validation)
	    storeRelease(http->old_entry);
	storeClientUnregister(http->old_sc, http->old_entry, http);
	storeUnlockObject(http->old_entry);
	recopy = 0;
    }
    http->old_entry = NULL;	/* done with old_entry */
    http->old_sc = NULL;
    if (http->request->flags.collapsed && !http->flags.hit && EBIT_TEST(entry->flags, RELEASE_REQUEST)) {
	/* Collapsed request, but the entry is not good to be sent */
	clientProcessMiss(http);
	return;
    }
    if (EBIT_TEST(entry->flags, ENTRY_ABORTED)) {
	/* Old object got aborted, not good */
	clientProcessMiss(http);
        return;
    }
    if (recopy) {
	storeClientCopyHeaders(http->sc, entry,
	    clientSendHeaders,
	    http);
    } else {
	clientSendHeaders(data, rep);
    }
}

