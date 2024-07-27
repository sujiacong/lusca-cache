#include "squid.h"

#include "hierarchy_entry.h"

#include "client_side_async_refresh.h"

/*
 * Perform an async refresh of an object
 */
typedef struct _clientAsyncRefreshRequest {
    request_t *request;
    StoreEntry *entry;
    StoreEntry *old_entry;
    store_client *sc;
    squid_off_t offset;
    size_t buf_in_use;
    struct timeval start;
} clientAsyncRefreshRequest;

CBDATA_TYPE(clientAsyncRefreshRequest);

static void
clientAsyncDone(clientAsyncRefreshRequest * async)
{
    AccessLogEntry al;
    static aclCheck_t *ch;
    MemObject *mem = async->entry->mem_obj;
    request_t *request = async->request;
    memset(&al, 0, sizeof(al));
    al.icp.opcode = ICP_INVALID;
    al.url = mem->url;
    debugs(33, 9, "clientAsyncDone: url='%s'", al.url);
    al.http.code = mem->reply->sline.status;
    al.http.content_type = strBuf(mem->reply->content_type);
    al.cache.size = async->offset;
    if (async->old_entry->mem_obj)
	async->old_entry->mem_obj->refresh_timestamp = 0;
    if (mem->reply->sline.status == 304) {
	/* Don't memcpy() the whole reply structure here.  For example,
	 * www.thegist.com (Netscape/1.13) returns a content-length for
	 * 304's which seems to be the length of the 304 HEADERS!!! and
	 * not the body they refer to.  */
	httpReplyUpdateOnNotModified(async->old_entry->mem_obj->reply, async->entry->mem_obj->reply);
	storeTimestampsSet(async->old_entry);
	storeUpdate(async->old_entry, async->request);
	al.cache.code = LOG_TCP_ASYNC_HIT;
    } else
	al.cache.code = LOG_TCP_ASYNC_MISS;
    al.cache.msec = tvSubMsec(async->start, current_time);
    if (Config.onoff.log_mime_hdrs) {
	Packer p;
	MemBuf mb;
	memBufDefInit(&mb);
	packerToMemInit(&p, &mb);
	httpHeaderPackInto(&request->header, &p);
	al.headers.request = xstrdup(mb.buf);
	packerClean(&p);
	memBufClean(&mb);
    }
    urlMethodAssign(&al.http.method, request->method);
    al.http.version = request->http_ver;
    hierarchyLogEntryCopy(&al.hier, &request->hier);
    if (request->auth_user_request) {
	if (authenticateUserRequestUsername(request->auth_user_request))
	    al.cache.authuser = xstrdup(authenticateUserRequestUsername(request->auth_user_request));
	authenticateAuthUserRequestUnlock(request->auth_user_request);
	request->auth_user_request = NULL;
    } else if (request->extacl_user) {
	al.cache.authuser = xstrdup(request->extacl_user);
    }
    al.request = request;
    al.reply = mem->reply;
    ch = aclChecklistCreate(Config.accessList.http, request, NULL);
    ch->reply = mem->reply;
    if (!Config.accessList.log || aclCheckFast(Config.accessList.log, ch))
	accessLogLog(&al, ch);
    aclChecklistFree(ch);
    storeClientUnregister(async->sc, async->entry, async);
    storeUnlockObject(async->entry->mem_obj->old_entry);
    async->entry->mem_obj->old_entry = NULL;
    storeUnlockObject(async->entry);
    storeUnlockObject(async->old_entry);
    requestUnlink(async->request);
    safe_free(al.headers.request);
    safe_free(al.headers.reply);
    safe_free(al.cache.authuser);
    cbdataFree(async);
}

static void
clientHandleAsyncReply(void *data, mem_node_ref nr, ssize_t size)
{
    clientAsyncRefreshRequest *async = data;
    StoreEntry *e = async->entry;
    stmemNodeUnref(&nr);
    if (EBIT_TEST(e->flags, ENTRY_ABORTED)) {
	clientAsyncDone(async);
	return;
    }
    if (size <= 0) {
	clientAsyncDone(async);
	return;
    }
    async->offset += size;
    if (e->mem_obj->reply->sline.status == 304) {
	clientAsyncDone(async);
	return;
    }
    storeClientRef(async->sc, async->entry,
	async->offset,
	async->offset,
	SM_PAGE_SIZE,
	clientHandleAsyncReply,
	async);
}

void
clientAsyncRefresh(clientHttpRequest * http)
{
    char *url = http->uri;
    clientAsyncRefreshRequest *async;
    request_t *request = http->request;
    debugs(33, 3, "clientAsyncRefresh: '%s'", http->uri);
    CBDATA_INIT_TYPE(clientAsyncRefreshRequest);
    http->entry->mem_obj->refresh_timestamp = squid_curtime;
    async = cbdataAlloc(clientAsyncRefreshRequest);
    async->start = current_time;
    async->request = requestLink(request);
    async->old_entry = http->entry;
    storeLockObject(async->old_entry);
    async->entry = storeCreateEntry(url,
	request->flags,
	request->method);
    if (request->store_url)
	storeEntrySetStoreUrl(async->entry, request->store_url);
    async->entry->mem_obj->old_entry = async->old_entry;
    storeLockObject(async->entry->mem_obj->old_entry);
    async->sc = storeClientRegister(async->entry, async);
    request->etags = NULL;	/* Should always be null as this was a cache hit, but just in case.. */
    httpHeaderDelById(&request->header, HDR_RANGE);
    httpHeaderDelById(&request->header, HDR_IF_RANGE);
    httpHeaderDelById(&request->header, HDR_IF_NONE_MATCH);
    httpHeaderDelById(&request->header, HDR_IF_MATCH);
    if (async->old_entry->lastmod > 0)
	request->lastmod = async->old_entry->lastmod;
    else if (async->old_entry->mem_obj && async->old_entry->mem_obj->reply)
	request->lastmod = async->old_entry->mem_obj->reply->date;
    else
	request->lastmod = -1;
    if (!request->etag) {
	const char *etag = httpHeaderGetStr(&async->old_entry->mem_obj->reply->header, HDR_ETAG);
	if (etag)
	    async->request->etag = xstrdup(etag);
    }
#if DELAY_POOLS
    /* delay_id is already set on original store client */
    delaySetStoreClient(async->sc, delayClient(http));
#endif
    fwdStart(-1, async->entry, async->request);
    storeClientRef(async->sc, async->entry,
	async->offset,
	async->offset,
	SM_PAGE_SIZE,
	clientHandleAsyncReply,
	async);
}
