
#include "squid.h"

#include "client_side.h"
#include "client_side_purge.h"


/*
 * The client-side purge path is a bit convoluted.
 *
 * The initial PURGE request will cause a call to clientPurgeRequest() via
 * clientProcessRequest(). clientProcessRequest() will then schedule
 * a lookup to swap the object in via clientCacheHit().
 *
 * clientCacheHit() will then parse the headers and then eventually kick
 * the request either back here directly or in the case of a swap in
 * failure, indirectly via clientProcessMiss().
 *
 * Once the headers have been swapped in the purge request completes
 * normally. The GET/HEAD method objects are individually poked
 * to be removed; there's some Vary magic going on and then a response
 * is returned to indicate whether the object was PURGEd or not.
 */

/*
 * Importantly, there's some very basic processing in clientProcessMiss()
 * and some Vary related processing in clientCacheHit(). This ends up
 * forming part of the PURGE processing. All of that interaction is almost
 * completely undocumented and likely poorly understood. So be careful.
 */
void
clientPurgeRequest(clientHttpRequest * http)
{
    StoreEntry *entry;
    ErrorState *err = NULL;
    HttpReply *r;
    http_status status = HTTP_NOT_FOUND;
    method_t *method_get = NULL, *method_head = NULL;
    debug(33, 3) ("Config2.onoff.enable_purge = %d\n", Config2.onoff.enable_purge);
    if (!Config2.onoff.enable_purge) {
	http->log_type = LOG_TCP_DENIED;
	err = errorCon(ERR_ACCESS_DENIED, HTTP_FORBIDDEN, http->orig_request);
	http->entry = clientCreateStoreEntry(http, http->request->method, null_request_flags);
	errorAppendEntry(http->entry, err);
	return;
    }
    /* Release both IP cache */
    ipcacheInvalidate(http->request->host);

    method_get = urlMethodGetKnownByCode(METHOD_GET);
    method_head = urlMethodGetKnownByCode(METHOD_HEAD);

    if (!http->flags.purging) {
	/* Try to find a base entry */
	http->flags.purging = 1;
	entry = storeGetPublicByRequestMethod(http->request, method_get);
	if (!entry) {
	    entry = storeGetPublicByRequestMethod(http->request, method_head);
	}
	if (entry) {
	    if (EBIT_TEST(entry->flags, ENTRY_SPECIAL)) {
		http->log_type = LOG_TCP_DENIED;
		err = errorCon(ERR_ACCESS_DENIED, HTTP_FORBIDDEN, http->request);
		http->entry = clientCreateStoreEntry(http, http->request->method, null_request_flags);
		errorAppendEntry(http->entry, err);
		return;
	    }
	    /* Swap in the metadata */
	    http->entry = entry;
	    storeLockObject(http->entry);
	    storeCreateMemObject(http->entry, http->uri);
	    urlMethodAssign(&http->entry->mem_obj->method, http->request->method);
	    http->sc = storeClientRegister(http->entry, http);
	    http->log_type = LOG_TCP_HIT;
	    storeClientCopyHeaders(http->sc, http->entry,
		clientCacheHit,
		http);
	    return;
	}
    }
    http->log_type = LOG_TCP_MISS;
    /* Release the cached URI */
    entry = storeGetPublicByRequestMethod(http->request, method_get);
    if (entry) {
	debug(33, 4) ("clientPurgeRequest: GET '%s'\n",
	    storeUrl(entry));
#if USE_HTCP
	neighborsHtcpClear(entry, NULL, http->request, method_get, HTCP_CLR_PURGE);
#endif
	storeRelease(entry);
	status = HTTP_OK;
    }
    entry = storeGetPublicByRequestMethod(http->request, method_head);
    if (entry) {
	debug(33, 4) ("clientPurgeRequest: HEAD '%s'\n",
	    storeUrl(entry));
#if USE_HTCP
	neighborsHtcpClear(entry, NULL, http->request, method_head, HTCP_CLR_PURGE);
#endif
	storeRelease(entry);
	status = HTTP_OK;
    }
    /* And for Vary, release the base URI if none of the headers was included in the request */
    if (http->request->vary_headers && !strstr(http->request->vary_headers, "=")) {
	entry = storeGetPublic(urlCanonical(http->request), method_get);
	if (entry) {
	    debug(33, 4) ("clientPurgeRequest: Vary GET '%s'\n",
		storeUrl(entry));
#if USE_HTCP
	    neighborsHtcpClear(entry, NULL, http->request, method_get, HTCP_CLR_PURGE);
#endif
	    storeRelease(entry);
	    status = HTTP_OK;
	}
	entry = storeGetPublic(urlCanonical(http->request), method_head);
	if (entry) {
	    debug(33, 4) ("clientPurgeRequest: Vary HEAD '%s'\n",
		storeUrl(entry));
#if USE_HTCP
	    neighborsHtcpClear(entry, NULL, http->request, method_head, HTCP_CLR_PURGE);
#endif
	    storeRelease(entry);
	    status = HTTP_OK;
	}
    }
    /*
     * Make a new entry to hold the reply to be written
     * to the client.
     */
    http->entry = clientCreateStoreEntry(http, http->request->method, null_request_flags);
    httpReplyReset(r = http->entry->mem_obj->reply);
    httpReplySetHeaders(r, status, NULL, NULL, 0, -1, squid_curtime);
    httpReplySwapOut(r, http->entry);
    storeComplete(http->entry);
}
