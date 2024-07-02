#include "squid.h"

#include "client_side_etag.h"
#include "client_side.h"
#include "store_vary.h"

static void
clientHandleETagMiss(clientHttpRequest * http)
{
    StoreEntry *entry = http->entry;
    request_t *request = http->request;

    request->done_etag = 1;
    if (request->vary) {
	storeLocateVaryDone(request->vary);
	request->vary = NULL;
	request->etags = NULL;	/* pointed into request->vary */
    }
    safe_free(request->etag);
    safe_free(request->vary_headers);
    safe_free(request->vary_hdr);
    storeClientUnregister(http->sc, entry, http);
    storeUnlockObject(entry);
    http->entry = NULL;
    clientProcessRequest(http);
}

static void
clientHandleETagReply(void *data, HttpReply * rep)
{
    //const char *buf = ref.node->data + ref.offset;
    clientHttpRequest *http = data;
    StoreEntry *entry = http->entry;
    const char *url = storeLookupUrl(entry);
    if (entry == NULL) {
	/* client aborted */
	return;
    }
    if (!rep) {
	debug(33, 3) ("clientHandleETagReply: FAILED '%s'\n", url);
	clientHandleETagMiss(http);
	return;
    }
    if (EBIT_TEST(entry->flags, ENTRY_ABORTED)) {
	debug(33, 3) ("clientHandleETagReply: ABORTED '%s'\n", url);
	clientHandleETagMiss(http);
	return;
    }
    debug(33, 3) ("clientHandleETagReply: %s = %d\n", url, (int) rep->sline.status);
    if (HTTP_NOT_MODIFIED == rep->sline.status) {
	/* Remember the ETag and restart */
	if (rep) {
	    request_t *request = http->request;
	    const char *etag = httpHeaderGetStr(&rep->header, HDR_ETAG);
	    const char *vary = request->vary_headers;
	    int has_vary = httpHeaderHas(&rep->header, HDR_VARY);
#if X_ACCELERATOR_VARY
	    has_vary |= httpHeaderHas(&rep->header, HDR_X_ACCELERATOR_VARY);
#endif
	    if (has_vary)
		vary = httpMakeVaryMark(request, rep);

	    if (etag && vary) {
		char *str;
		str = stringDupToC(&request->vary_encoding);
		storeAddVary(entry->mem_obj->store_url, entry->mem_obj->url, entry->mem_obj->method, NULL, httpHeaderGetStr(&rep->header, HDR_ETAG), request->vary_hdr, request->vary_headers, str);
		safe_free(str);
	    }
	}
	clientHandleETagMiss(http);
	return;
    }
    /* Send the new object to the client */
    clientSendHeaders(data, rep);
    return;
}

void
clientProcessETag(clientHttpRequest * http)
{
    char *url = http->uri;
    StoreEntry *entry = NULL;
    debug(33, 3) ("clientProcessETag: '%s'\n", http->uri);
    entry = storeCreateEntry(url, http->request->flags, http->request->method);
    if (http->request->store_url)
	storeEntrySetStoreUrl(entry, http->request->store_url);
    http->sc = storeClientRegister(entry, http);
#if DELAY_POOLS
    /* delay_id is already set on original store client */
    delaySetStoreClient(http->sc, delayClient(http));
#endif
    http->entry = entry;
    http->out.offset = 0;
    fwdStart(http->conn->fd, http->entry, http->request);
    /* Register with storage manager to receive updates when data comes in. */
    if (EBIT_TEST(entry->flags, ENTRY_ABORTED))
	debug(33, 0) ("clientProcessETag: found ENTRY_ABORTED object\n");
    storeClientCopyHeaders(http->sc, entry,
	clientHandleETagReply,
	http);
}
