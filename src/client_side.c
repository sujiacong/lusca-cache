
/*
 * $Id: client_side.c 14747 2010-08-06 06:37:10Z adrian.chadd $
 *
 * DEBUG: section 33    Client-side Routines
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

#include "client_db.h"
#include "hierarchy_entry.h"

#include "client_side_conn.h"
#include "client_side_body.h"
#include "client_side_request.h"
#include "client_side_ranges.h"
#include "client_side_async_refresh.h"
#include "client_side_refresh.h"
#include "client_side_etag.h"
#include "client_side_ims.h"
#include "client_side_purge.h"
#include "client_side_request_parse.h"

#include "client_side.h"

#include "store_vary.h"


#if LINGERING_CLOSE
#define comm_close comm_lingering_close
#endif

static const char *const crlf = "\r\n";

#define FAILURE_MODE_TIME 300


/* Local functions */

static CWCB clientWriteComplete;
static CWCB clientWriteBodyComplete;
static PF clientReadRequest;
static PF requestTimeout;
static int clientCheckTransferDone(clientHttpRequest *);
static int clientGotNotEnough(clientHttpRequest *);
static void checkFailureRatio(err_type, hier_code);
static void clientBuildReplyHeader(clientHttpRequest * http, HttpReply * rep);

#if USE_IDENT
static IDCB clientIdentDone;
#endif

static STNCB clientSendMoreData;
static int clientCachable(clientHttpRequest * http);
static int clientHierarchical(clientHttpRequest * http);
static DEFER httpAcceptDefer;
static log_type clientProcessRequest2(clientHttpRequest * http);
static int clientReplyBodyTooLarge(clientHttpRequest *, squid_off_t clen);
#if USE_SSL
static void httpsAcceptSSL(ConnStateData * connState, SSL_CTX * sslContext);
#endif
static int varyEvaluateMatch(StoreEntry * entry, request_t * request);
static int clientCheckBeginForwarding(clientHttpRequest * http);

#if USE_IDENT
static void
clientIdentDone(const char *ident, void *data)
{
    ConnStateData *conn = data;
    xstrncpy(conn->rfc931, ident ? ident : dash_str, USER_IDENT_SZ);
}

#endif

aclCheck_t *
clientAclChecklistCreate(const acl_access * acl, const clientHttpRequest * http)
{
    aclCheck_t *ch;
    ConnStateData *conn = http->conn;
    ch = aclChecklistCreate(acl,
	http->request,
	conn->rfc931);

    /*
     * hack for ident ACL. It needs to get full addresses, and a
     * place to store the ident result on persistent connections...
     */
    /* connection oriented auth also needs these two lines for it's operation. */
    ch->conn = conn;
    cbdataLock(ch->conn);

    return ch;
}

/*
 * returns true if client specified that the object must come from the cache
 * without contacting origin server
 */
int
clientOnlyIfCached(clientHttpRequest * http)
{
    const request_t *r = http->request;
    assert(r);
    return r->cache_control &&
	EBIT_TEST(r->cache_control->mask, CC_ONLY_IF_CACHED);
}

/*
 * Create a store entry for the given request information.
 *
 * the method_t passed in is not "given" to the request; it is still
 * the responsibility of the caller to free.
 */
StoreEntry *
clientCreateStoreEntry(clientHttpRequest * h, method_t * m, request_flags flags)
{
    StoreEntry *e;
    /*
     * For erroneous requests, we might not have a h->request,
     * so make a fake one.
     */
    if (h->request == NULL)
	h->request = requestLink(requestCreate(m, PROTO_NONE, null_string));
    e = storeCreateEntry(h->uri, flags, m);
    if (h->request->store_url)
	storeEntrySetStoreUrl(e, h->request->store_url);
    h->sc = storeClientRegister(e, h);
#if DELAY_POOLS
    if (h->log_type != LOG_TCP_DENIED)
	delaySetStoreClient(h->sc, delayClient(h));
#endif
    storeClientCopyHeaders(h->sc, e, clientSendHeaders, h);
    return e;
}

int
checkNegativeHit(StoreEntry * e)
{
    if (!EBIT_TEST(e->flags, ENTRY_NEGCACHED))
	return 0;
    if (e->expires <= squid_curtime)
	return 0;
    if (e->store_status != STORE_OK)
	return 0;
    return 1;
}

static void
clientUpdateCounters(clientHttpRequest * http)
{
    int svc_time = tvSubMsec(http->start, current_time);
    ping_data *i;
    HierarchyLogEntry *H;
    statCounter.client_http.requests++;
    if (isTcpHit(http->log_type))
	statCounter.client_http.hits++;
    if (http->log_type == LOG_TCP_HIT)
	statCounter.client_http.disk_hits++;
    else if (http->log_type == LOG_TCP_MEM_HIT)
	statCounter.client_http.mem_hits++;
    if (http->request->err_type != ERR_NONE)
	statCounter.client_http.errors++;
    statHistCount(&statCounter.client_http.all_svc_time, svc_time);
    /*
     * The idea here is not to be complete, but to get service times
     * for only well-defined types.  For example, we don't include
     * LOG_TCP_REFRESH_FAIL_HIT because its not really a cache hit
     * (we *tried* to validate it, but failed).
     */
    switch (http->log_type) {
    case LOG_TCP_REFRESH_HIT:
	statHistCount(&statCounter.client_http.nh_svc_time, svc_time);
	break;
    case LOG_TCP_IMS_HIT:
	statHistCount(&statCounter.client_http.nm_svc_time, svc_time);
	break;
    case LOG_TCP_HIT:
    case LOG_TCP_MEM_HIT:
    case LOG_TCP_OFFLINE_HIT:
	statHistCount(&statCounter.client_http.hit_svc_time, svc_time);
	break;
    case LOG_TCP_MISS:
    case LOG_TCP_CLIENT_REFRESH_MISS:
	statHistCount(&statCounter.client_http.miss_svc_time, svc_time);
	break;
    default:
	/* make compiler warnings go away */
	break;
    }
    H = &http->request->hier;
    switch (H->code) {
#if USE_CACHE_DIGESTS
    case CD_PARENT_HIT:
    case CD_SIBLING_HIT:
	statCounter.cd.times_used++;
	break;
#endif
    case SIBLING_HIT:
    case PARENT_HIT:
    case FIRST_PARENT_MISS:
    case CLOSEST_PARENT_MISS:
	statCounter.icp.times_used++;
	i = &H->ping;
	if (0 != i->stop.tv_sec && 0 != i->start.tv_sec)
	    statHistCount(&statCounter.icp.query_svc_time,
		tvSubUsec(i->start, i->stop));
	if (i->timeout)
	    statCounter.icp.query_timeouts++;
	break;
    case CLOSEST_PARENT:
    case CLOSEST_DIRECT:
	statCounter.netdb.times_used++;
	break;
    default:
	break;
    }
}

/*
 * The bulk of the client-side request/reply logging code.
 *
 * Note that some of this code handles non-logging cleanups - 
 * mostly because it's firstly just a straight refactor from
 * the original source code with minimal program flow changes.
 * I'll investigate what needs changing in a later commit
 * to be sure that the modifications in question don't
 * inadvertently break things.
 *
 * This function also updates the clientdb.
 */
static void
httpRequestLog(clientHttpRequest *http)
{
    MemObject *mem = NULL;
    request_t *request = http->request;
    ConnStateData *conn = http->conn;

    assert(http->log_type < LOG_TYPE_MAX);
    if (http->entry)
	mem = http->entry->mem_obj;
    if (http->out.size || http->log_type) {
	http->al.icp.opcode = ICP_INVALID;
	http->al.url = http->log_uri;
	if (!http->al.url)
	    http->al.url = urlCanonicalClean(request);
	debug(33, 9) ("httpRequestLog: al.url='%s'\n", http->al.url);
	http->al.cache.out_ip = request->out_ip;
	if (http->reply && http->log_type != LOG_TCP_DENIED) {
	    http->al.http.code = http->reply->sline.status;
	    http->al.http.content_type = strBuf(http->reply->content_type);
	} else if (mem) {
	    http->al.http.code = mem->reply->sline.status;
	    http->al.http.content_type = strBuf(mem->reply->content_type);
	}
	http->al.cache.caddr = conn->log_addr;
	http->al.cache.size = http->out.size;
	http->al.cache.code = http->log_type;
	http->al.cache.msec = tvSubMsec(http->start, current_time);
	http->al.cache.rq_size = http->req_sz;
	http->al.cache.client_tos = http->client_tos;
	if (request) {
	    http->al.cache.rq_size += request->content_length;
	    if (Config.onoff.log_mime_hdrs) {
		Packer p;
		MemBuf mb;
		memBufDefInit(&mb);
		packerToMemInit(&p, &mb);
		httpHeaderPackInto(&request->header, &p);
		http->al.headers.request = xstrdup(mb.buf);
		packerClean(&p);
		memBufClean(&mb);
	    }
	    urlMethodAssign(&http->al.http.method, request->method);
	    http->al.http.version = request->http_ver;
	    hierarchyLogEntryCopy(&http->al.hier, &request->hier);
	    if (request->auth_user_request) {
		if (authenticateUserRequestUsername(request->auth_user_request))
		    http->al.cache.authuser = xstrdup(authenticateUserRequestUsername(request->auth_user_request));
		authenticateAuthUserRequestUnlock(request->auth_user_request);
		request->auth_user_request = NULL;
	    } else if (request->extacl_user) {
		http->al.cache.authuser = xstrdup(request->extacl_user);
	    }
	    if (conn->rfc931[0])
		http->al.cache.rfc931 = conn->rfc931;
	}
#if USE_SSL
	http->al.cache.ssluser = sslGetUserEmail(fd_table[conn->fd].ssl);
#endif
	http->al.request = request;
	if (!http->acl_checklist)
	    http->acl_checklist = clientAclChecklistCreate(Config.accessList.http, http);
	http->acl_checklist->reply = http->reply;
	if (!Config.accessList.log || aclCheckFast(Config.accessList.log, http->acl_checklist)) {
	    http->al.reply = http->reply;
	    accessLogLog(&http->al, http->acl_checklist);
	    clientUpdateCounters(http);
	    clientdbUpdate(conn->peer.sin_addr, http->log_type, PROTO_HTTP, http->out.size);
	}
    }
    safe_free(http->al.headers.request);
    safe_free(http->al.headers.reply);
    safe_free(http->al.cache.authuser);
    http->al.request = NULL;
}

void
httpRequestFree(void *data)
{
    clientHttpRequest *http = data;
    StoreEntry *e;
    request_t *request = http->request;

    debug(33, 3) ("httpRequestFree: %s\n", storeUrl(http->entry));
    if (!clientCheckTransferDone(http)) {
	requestAbortBody(request);	/* abort request body transter */
	/* HN: This looks a bit odd.. why should client_side care about
	 * the ICP selection status?
	 */
	if (http->entry && http->entry->ping_status == PING_WAITING)
	    storeReleaseRequest(http->entry);
    }

    httpRequestLog(http);

    /* XXX accesslog struct used here outside of httpRequestLog() ! */
    if (request)
	checkFailureRatio(request->err_type, http->al.hier.code);

    if (http->acl_checklist)
	aclChecklistFree(http->acl_checklist);
    safe_free(http->uri);
    safe_free(http->log_uri);
    safe_free(http->redirect.location);
    stringClean(&http->range_iter.boundary);
    if (http->old_entry && http->old_entry->mem_obj && http->old_entry->mem_obj->ims_entry && http->old_entry->mem_obj->ims_entry == http->entry) {
	storeUnlockObject(http->old_entry->mem_obj->ims_entry);
	http->old_entry->mem_obj->ims_entry = NULL;
    }
    if ((e = http->entry)) {
	http->entry = NULL;
	storeClientUnregister(http->sc, e, http);
	http->sc = NULL;
	storeUnlockObject(e);
    }
    /* old_entry might still be set if we didn't yet get the reply
     * code in clientHandleIMSReply() */
    if ((e = http->old_entry)) {
	http->old_entry = NULL;
	storeClientUnregister(http->old_sc, e, http);
	http->old_sc = NULL;
	storeUnlockObject(e);
    }
    requestUnlink(http->request);
    http->request = NULL;
    requestUnlink(http->orig_request);
    http->orig_request = NULL;
    if (http->reply)
	httpReplyDestroy(http->reply);
    http->reply = NULL;
    assert(DLINK_HEAD(http->conn->reqs) != NULL);
    /* Unlink us from the clients request list */
    dlinkDelete(&http->node, &http->conn->reqs);
    dlinkDelete(&http->active, &ClientActiveRequests);
    cbdataFree(http);
}

/*
 * Interpret the request headers after the initial request has
 * been read, parsed and modified appropriately.
 *
 * This sets various variables and flags in http->request based on
 * the request headers. It also sets up the pinned connection
 * link if needed!
 */
void
clientInterpretRequestHeaders(clientHttpRequest * http)
{
    request_t *request = http->request;
    HttpHeader *req_hdr = &request->header;
    int no_cache = 0;
    const char *str;
    request->imslen = -1;
    request->ims = httpHeaderGetTime(req_hdr, HDR_IF_MODIFIED_SINCE);
    if (request->ims > 0)
	request->flags.ims = 1;
    if (httpHeaderHas(req_hdr, HDR_PRAGMA)) {
	String s = httpHeaderGetList(req_hdr, HDR_PRAGMA);
	if (strListIsMember(&s, "no-cache", ','))
	    no_cache++;
	stringClean(&s);
    }
    assert(request->cache_control == NULL);
    request->cache_control = httpHeaderGetCc(req_hdr);
    if (request->cache_control)
	if (EBIT_TEST(request->cache_control->mask, CC_NO_CACHE))
	    no_cache++;
    /* Work around for supporting the Reload button in IE browsers
     * when Squid is used as an accelerator or transparent proxy,
     * by turning accelerated IMS request to no-cache requests.
     * Now knows about IE 5.5 fix (is actually only fixed in SP1, 
     * but we can't tell whether we are talking to SP1 or not so 
     * all 5.5 versions are treated 'normally').
     */
    if (Config.onoff.ie_refresh) {
	if (http->flags.accel && request->flags.ims) {
	    if ((str = httpHeaderGetStr(req_hdr, HDR_USER_AGENT))) {
		if (strstr(str, "MSIE 5.01") != NULL)
		    no_cache++;
		else if (strstr(str, "MSIE 5.0") != NULL)
		    no_cache++;
		else if (strstr(str, "MSIE 4.") != NULL)
		    no_cache++;
		else if (strstr(str, "MSIE 3.") != NULL)
		    no_cache++;
	    }
	}
    }
    if (no_cache) {
#if HTTP_VIOLATIONS
	if (Config.onoff.reload_into_ims)
	    request->flags.nocache_hack = 1;
	else if (refresh_nocache_hack)
	    request->flags.nocache_hack = 1;
	else
#endif
	    request->flags.nocache = 1;
    }
    if (http->conn->port->no_connection_auth)
	request->flags.no_connection_auth = 1;
    if (Config.onoff.pipeline_prefetch)
	request->flags.no_connection_auth = 1;

    /* ignore range header in non-GETs */
    if (request->method->code == METHOD_GET) {
	request->range = httpHeaderGetRange(req_hdr);
	if (request->range)
	    request->flags.range = 1;
    }
    if (httpHeaderHas(req_hdr, HDR_AUTHORIZATION))
	request->flags.auth = 1;
    else if (request->login[0] != '\0')
	request->flags.auth = 1;
    if (request->flags.no_connection_auth) {
	/* nothing special to do here.. */
    } else if (http->conn->pinning.fd != -1) {
	if (http->conn->pinning.auth) {
	    request->flags.connection_auth = 1;
	    request->flags.auth = 1;
	} else {
	    request->flags.connection_proxy_auth = 1;
	}
	request->pinned_connection = http->conn;
	cbdataLock(request->pinned_connection);
    }
    /* check if connection auth is used, and flag as candidate for pinning
     * in such case.
     * Note: we may need to set flags.connection_auth even if the connection
     * is already pinned if it was pinned earlier due to proxy auth
     */
    if (request->flags.connection_auth) {
	/* already taken care of above */
    } else if (httpHeaderHas(req_hdr, HDR_AUTHORIZATION) || httpHeaderHas(req_hdr, HDR_PROXY_AUTHORIZATION)) {
	HttpHeaderPos pos = HttpHeaderInitPos;
	HttpHeaderEntry *e;
	int may_pin = 0;
	while ((e = httpHeaderGetEntry(req_hdr, &pos))) {
	    if (e->id == HDR_AUTHORIZATION || e->id == HDR_PROXY_AUTHORIZATION) {
		if (strNCaseCmp(e->value, "NTLM ", 5) == 0
		    ||
		    strNCaseCmp(e->value, "Negotiate ", 10) == 0
		    ||
		    strNCaseCmp(e->value, "Kerberos ", 9) == 0) {
		    if (e->id == HDR_AUTHORIZATION) {
			request->flags.connection_auth = 1;
			may_pin = 1;
		    } else {
			request->flags.connection_proxy_auth = 1;
			may_pin = 1;
		    }
		}
	    }
	}
	if (may_pin && !request->pinned_connection) {
	    request->pinned_connection = http->conn;
	    cbdataLock(request->pinned_connection);
	}
    }
    if (httpHeaderHas(req_hdr, HDR_VIA)) {
	/*
	 * ThisCache cannot be a member of Via header, "1.0 ThisCache" can.
	 * Note ThisCache2 has a space prepended to the hostname so we don't
	 * accidentally match super-domains.
	 */
	String s = httpHeaderGetList(req_hdr, HDR_VIA);
	int n = strIsSubstr(&s, ThisCache2);
	if (n) {
	    debugObj(33, 1, "WARNING: Forwarding loop detected for:\n",
		request, (ObjPackMethod) & httpRequestPackDebug);
	    request->flags.loopdetect = 1;
	    if (n > 1)
		request->flags.loopdetect_twice = 1;
	}
#if FORW_VIA_DB
	fvdbCountVia(strBuf(s));
#endif
	stringClean(&s);
    }
#if USE_USERAGENT_LOG
    if ((str = httpHeaderGetStr(req_hdr, HDR_USER_AGENT)))
	logUserAgent(fqdnFromAddr(http->conn->log_addr), str);
#endif
#if USE_REFERER_LOG
    if ((str = httpHeaderGetStr(req_hdr, HDR_REFERER)))
	logReferer(fqdnFromAddr(http->conn->log_addr), str, rfc1738_escape_unescaped(http->uri));
#endif
#if FORW_VIA_DB
    if (httpHeaderHas(req_hdr, HDR_X_FORWARDED_FOR)) {
	String s = httpHeaderGetList(req_hdr, HDR_X_FORWARDED_FOR);
	fvdbCountForw(strBuf(s));
	stringClean(&s);
    }
#endif
    if (request->method->code == METHOD_TRACE) {
	request->max_forwards = httpHeaderGetInt(req_hdr, HDR_MAX_FORWARDS);
    }
    if (clientCachable(http))
	request->flags.cachable = 1;
    if (clientHierarchical(http))
	request->flags.hierarchical = 1;
    debug(33, 5) ("clientInterpretRequestHeaders: REQ_NOCACHE = %s\n",
	request->flags.nocache ? "SET" : "NOT SET");
    debug(33, 5) ("clientInterpretRequestHeaders: REQ_CACHABLE = %s\n",
	request->flags.cachable ? "SET" : "NOT SET");
    debug(33, 5) ("clientInterpretRequestHeaders: REQ_HIERARCHICAL = %s\n",
	request->flags.hierarchical ? "SET" : "NOT SET");
}

static int
clientCachable(clientHttpRequest * http)
{
    request_t *req = http->request;
    method_t *method = req->method;
    if (req->flags.loopdetect)
	return 0;
    if (req->protocol == PROTO_HTTP)
	return httpCachable(method);
    /* FTP is always cachable */
    if (method->code == METHOD_OTHER)
	return 0;
    if (method->code == METHOD_CONNECT)
	return 0;
    if (method->code == METHOD_TRACE)
	return 0;
    if (method->code == METHOD_PUT)
	return 0;
    if (method->code == METHOD_POST)
	return 0;		/* XXX POST may be cached sometimes.. ignored for now */
    if (req->protocol == PROTO_GOPHER)
	return gopherCachable(req);
    if (req->protocol == PROTO_CACHEOBJ)
	return 0;
    return 1;
}

/* Return true if we can query our neighbors for this object */
static int
clientHierarchical(clientHttpRequest * http)
{
    const char *url = http->uri;
    request_t *request = http->request;
    method_t *method = request->method;
    const wordlist *p = NULL;

    /* IMS needs a private key, so we can use the hierarchy for IMS only
     * if our neighbors support private keys */
    if (request->flags.ims && !neighbors_do_private_keys)
	return 0;
    if (request->flags.auth)
	return 0;
    if (method->code == METHOD_TRACE)
	return 1;
    if (method->code != METHOD_GET)
	return 0;
    /* scan hierarchy_stoplist */
    for (p = Config.hierarchy_stoplist; p; p = p->next)
	if (strstr(url, p->key))
	    return 0;
    if (request->flags.loopdetect)
	return 0;
    if (request->protocol == PROTO_HTTP)
	return httpCachable(method);
    if (request->protocol == PROTO_GOPHER)
	return gopherCachable(request);
    if (request->protocol == PROTO_CACHEOBJ)
	return 0;
    return 1;
}

int
isTcpHit(log_type code)
{
    /* this should be a bitmap for better optimization */
    if (code == LOG_TCP_HIT)
	return 1;
    if (code == LOG_TCP_STALE_HIT)
	return 1;
    if (code == LOG_TCP_ASYNC_HIT)
	return 1;
    if (code == LOG_TCP_IMS_HIT)
	return 1;
    if (code == LOG_TCP_REFRESH_FAIL_HIT)
	return 1;
    if (code == LOG_TCP_REFRESH_HIT)
	return 1;
    if (code == LOG_TCP_NEGATIVE_HIT)
	return 1;
    if (code == LOG_TCP_MEM_HIT)
	return 1;
    if (code == LOG_TCP_OFFLINE_HIT)
	return 1;
    return 0;
}

/*
 * filters out unwanted entries from original reply header
 * adds extra entries if we have more info than origin server
 * adds Squid specific entries
 */
static void
clientBuildReplyHeader(clientHttpRequest * http, HttpReply * rep)
{
    HttpHeader *hdr = &rep->header;
    request_t *request = http->request;
    httpHeaderDelById(hdr, HDR_PROXY_CONNECTION);
    /* here: Keep-Alive is a field-name, not a connection directive! */
    httpHeaderDelById(hdr, HDR_KEEP_ALIVE);
    /* remove Set-Cookie if a hit */
    if (http->flags.hit)
	httpHeaderDelById(hdr, HDR_SET_COOKIE);
    httpHeaderDelById(hdr, HDR_TRAILER);
    httpHeaderDelById(hdr, HDR_TRANSFER_ENCODING);
    httpHeaderDelById(hdr, HDR_UPGRADE);
    /* handle Connection header */
    if (httpHeaderHas(hdr, HDR_CONNECTION)) {
	/* anything that matches Connection list member will be deleted */
	String strConnection = httpHeaderGetList(hdr, HDR_CONNECTION);
	const HttpHeaderEntry *e;
	HttpHeaderPos pos = HttpHeaderInitPos;
	int headers_deleted = 0;
	/*
	 * think: on-average-best nesting of the two loops (hdrEntry
	 * and strListItem) @?@
	 */
	while ((e = httpHeaderGetEntry(hdr, &pos))) {
	    if (e->id == HDR_KEEP_ALIVE)
		continue;	/* Common, and already taken care of above */
	    if (strListIsMember(&strConnection, strBuf(e->name), ',')) {
		httpHeaderDelAt(hdr, pos);
		headers_deleted++;
	    }
	}
	if (headers_deleted)
	    httpHeaderRefreshMask(hdr);
	httpHeaderDelById(hdr, HDR_CONNECTION);
	stringClean(&strConnection);
    }
    /* Handle Ranges */
    if (request->range)
	clientBuildRangeHeader(http, rep);
    /*
     * Add a estimated Age header on cache hits.
     */
    if (http->flags.hit) {
	/*
	 * Remove any existing Age header sent by upstream caches
	 * (note that the existing header is passed along unmodified
	 * on cache misses)
	 */
	httpHeaderDelById(hdr, HDR_AGE);
	/*
	 * This adds the calculated object age. Note that the details of the
	 * age calculation is performed by adjusting the timestamp in
	 * storeTimestampsSet(), not here.
	 *
	 * BROWSER WORKAROUND: IE sometimes hangs when receiving a 0 Age
	 * header, so don't use it unless there is a age to report. Please
	 * note that Age is only used to make a conservative estimation of
	 * the objects age, so a Age: 0 header does not add any useful
	 * information to the reply in any case.
	 */
	if (http->entry) {
	    if (EBIT_TEST(http->entry->flags, ENTRY_SPECIAL)) {
		httpHeaderDelById(hdr, HDR_DATE);
		httpHeaderInsertTime(hdr, 0, HDR_DATE, squid_curtime);
	    } else if (http->entry->timestamp < 0) {
		(void) 0;
	    } else if (http->conn->port->act_as_origin) {
		HttpHeaderEntry *h = httpHeaderFindEntry(hdr, HDR_DATE);
		if (h)
		    httpHeaderPutExt(hdr, "X-Origin-Date", strBuf2(h->value), strLen2(h->value));
		httpHeaderDelById(hdr, HDR_DATE);
		httpHeaderInsertTime(hdr, 0, HDR_DATE, squid_curtime);
		h = httpHeaderFindEntry(hdr, HDR_EXPIRES);
		if (h && http->entry->expires >= 0) {
		    httpHeaderPutExt(hdr, "X-Origin-Expires", strBuf2(h->value), strLen2(h->value));
		    httpHeaderDelById(hdr, HDR_EXPIRES);
		    httpHeaderInsertTime(hdr, 1, HDR_EXPIRES, squid_curtime + http->entry->expires - http->entry->timestamp);
		} {
		    char age[64];
		    snprintf(age, sizeof(age), "%ld", (long int) squid_curtime - http->entry->timestamp);
		    httpHeaderPutExt(hdr, "X-Cache-Age", age, -1);
		}
	    } else if (http->entry->timestamp < squid_curtime) {
		httpHeaderPutInt(hdr, HDR_AGE,
		    squid_curtime - http->entry->timestamp);
	    }
	    if (!httpHeaderHas(hdr, HDR_CONTENT_LENGTH) && http->entry->mem_obj && http->entry->store_status == STORE_OK) {
		rep->content_length = contentLen(http->entry);
		httpHeaderPutSize(hdr, HDR_CONTENT_LENGTH, rep->content_length);
	    }
	}
    }
    /* Filter unproxyable authentication types */
    if (http->log_type != LOG_TCP_DENIED &&
	(httpHeaderHas(hdr, HDR_WWW_AUTHENTICATE))) {
	HttpHeaderPos pos = HttpHeaderInitPos;
	HttpHeaderEntry *e;
	int connection_auth_blocked = 0;
	while ((e = httpHeaderGetEntry(hdr, &pos))) {
	    if (e->id == HDR_WWW_AUTHENTICATE) {
		const char *value = strBuf(e->value);
		if ((strncasecmp(value, "NTLM", 4) == 0 &&
			(value[4] == '\0' || value[4] == ' '))
		    ||
		    (strncasecmp(value, "Negotiate", 9) == 0 &&
			(value[9] == '\0' || value[9] == ' '))
		    ||
		    (strncasecmp(value, "Kerberos", 8) == 0 &&
			(value[8] == '\0' || value[8] == ' '))) {
		    if (request->flags.no_connection_auth) {
			httpHeaderDelAt(hdr, pos);
			connection_auth_blocked = 1;
			continue;
		    }
		    request->flags.must_keepalive = 1;
		    if (!request->flags.accelerated && !request->flags.transparent) {
			httpHeaderPutStr(hdr, HDR_PROXY_SUPPORT, "Session-Based-Authentication");
			httpHeaderPutStr(hdr, HDR_CONNECTION, "Proxy-support");
		    }
		    break;
		}
	    }
	}
	if (connection_auth_blocked)
	    httpHeaderRefreshMask(hdr);
    }
    /* Handle authentication headers */
    if (request->auth_user_request)
	authenticateFixHeader(rep, request->auth_user_request, request, http->flags.accel, 0);
    /* Append X-Cache */
    httpHeaderPutStrf(hdr, HDR_X_CACHE, "%s from %s",
	http->flags.hit ? "HIT" : "MISS", getMyHostname());
#if USE_CACHE_DIGESTS
    /* Append X-Cache-Lookup: -- temporary hack, to be removed @?@ @?@ */
    httpHeaderPutStrf(hdr, HDR_X_CACHE_LOOKUP, "%s from %s:%d",
	http->lookup_type ? http->lookup_type : "NONE",
	getMyHostname(), getMyPort());
#endif
    if (httpReplyBodySize(request->method, rep) < 0) {
	if (http->conn->port->http11 && (request->http_ver.major > 1 || (request->http_ver.major == 1 && request->http_ver.minor >= 1))) {
	    debug(33, 2) ("clientBuildReplyHeader: send chunked response, unknown body size\n");
	    request->flags.chunked_response = 1;
	} else {
	    debug(33, 3) ("clientBuildReplyHeader: can't keep-alive, unknown body size\n");
	    request->flags.proxy_keepalive = 0;
	}
    }
    if (fdUsageHigh() && !request->flags.must_keepalive) {
	debug(33, 3) ("clientBuildReplyHeader: Not many unused FDs, can't keep-alive\n");
	request->flags.proxy_keepalive = 0;
    }
    if (!Config.onoff.error_pconns && rep->sline.status >= 400 && !request->flags.must_keepalive) {
	debug(33, 3) ("clientBuildReplyHeader: Error, don't keep-alive\n");
	request->flags.proxy_keepalive = 0;
    }
    if (!Config.onoff.client_pconns && !request->flags.must_keepalive)
	request->flags.proxy_keepalive = 0;
    if (request->flags.connection_auth && !rep->keep_alive) {
	debug(33, 2) ("clientBuildReplyHeader: Connection oriented auth but server side non-persistent\n");
	request->flags.proxy_keepalive = 0;
    }
    /* Append Transfer-Encoding */
    if (request->flags.chunked_response) {
	httpHeaderPutStr(hdr, HDR_TRANSFER_ENCODING, "chunked");
    }
    /* Append Via */
    if (Config.onoff.via && http->entry) {
	LOCAL_ARRAY(char, bbuf, MAX_URL + 32);
	String strVia = httpHeaderGetList(hdr, HDR_VIA);
	snprintf(bbuf, MAX_URL + 32, "%d.%d %s",
	    rep->sline.version.major,
	    rep->sline.version.minor, ThisCache);
	strListAdd(&strVia, bbuf, ',');
	httpHeaderDelById(hdr, HDR_VIA);
	httpHeaderPutString(hdr, HDR_VIA, &strVia);
	stringClean(&strVia);
    }
    /* Signal keep-alive if needed */
    if (!request->flags.proxy_keepalive)
	httpHeaderPutStr(hdr, HDR_CONNECTION, "close");
    else if ((request->http_ver.major == 1 && request->http_ver.minor == 0) || !http->conn->port->http11) {
	httpHeaderPutStr(hdr, HDR_CONNECTION, "keep-alive");
	if (!(http->flags.accel || http->flags.transparent))
	    httpHeaderPutStr(hdr, HDR_PROXY_CONNECTION, "keep-alive");
    }
#if ADD_X_REQUEST_URI
    /*
     * Knowing the URI of the request is useful when debugging persistent
     * connections in a client; we cannot guarantee the order of http headers,
     * but X-Request-URI is likely to be the very last header to ease use from a
     * debugger [hdr->entries.count-1].
     */
    httpHeaderPutStr(hdr, HDR_X_REQUEST_URI,
	http->entry->mem_obj->url ? http->entry->mem_obj->url : http->uri);
#endif
    httpHdrMangleList(hdr, request);
}

/* Used exclusively by clientCloneReply() during failure cases only */
static void
clientUnwindReply(clientHttpRequest * http, HttpReply * rep)
{
    if (rep != NULL) {
	httpReplyDestroy(rep);
	rep = NULL;
    }
    /* This destroys the range request */
    if (http->request->range)
	clientBuildRangeHeader(http, rep);
}

/*
 * This routine was historically called when we think we've got enough header
 * data - ie, after the first read. The store would not be allowed to release
 * data to be read until after all the headers were appended.
 *
 * So we, for now, just assume all the headers are here or they won't ever
 * be.
 */
static HttpReply *
clientCloneReply(clientHttpRequest * http, HttpReply * orig_rep)
{
    HttpReply *rep = NULL;
    /* If we don't have a memobj / reply by now then we're stuffed */
    if (http->sc->entry->mem_obj == NULL || http->sc->entry->mem_obj->reply == NULL) {
	clientUnwindReply(http, NULL);
	return NULL;
    }
    /* try to grab the already-parsed header */
    rep = httpReplyClone(orig_rep);
    if (rep->pstate == psParsed) {
	/* do header conversions */
	clientBuildReplyHeader(http, rep);
	/* if we do ranges, change status to "Partial Content" */
	if (http->request->range)
	    httpStatusLineSet(&rep->sline, rep->sline.version,
		HTTP_PARTIAL_CONTENT, NULL);
    } else {
	/* parsing failure, get rid of the invalid reply */
	clientUnwindReply(http, rep);
	return NULL;
    }
    return rep;
}

/*
 * clientProcessVary is called when it is detected that a object
 * varies and we need to get the correct variant
 */
static void
clientProcessVary(VaryData * vary, void *data)
{
    clientHttpRequest *http = data;
    if (!vary) {
	clientProcessRequest(http);
	return;
    }
    if (vary->key) {
	debug(33, 2) ("clientProcessVary: HIT key=%s etag=%s\n",
	    vary->key, vary->etag ? vary->etag : "NONE");
    } else {
	int i;
	debug(33, 2) ("clientProcessVary MISS\n");
	for (i = 0; i < vary->etags.count; i++) {
	    debug(33, 3) ("ETag: %s\n", (char *) vary->etags.items[i]);
	}
    }
    http->request->vary = vary;
    clientProcessRequest(http);
}

/*
 * This particular logic is a bit hairy.
 *
 * + If we have a store URL then we need to make sure the mem store url OR the mem url
 *   match the request store url.
 * + If we have no store URL then we need to make sure the mem url match the request url
 *   regardless of the store url (so objects which have store urls that match their urls
 *   can still be HIT fine.)
*/
static int
clientCheckUrlIsValid(clientHttpRequest *http)
{
	StoreEntry *e = http->entry;
	MemObject *mem = e->mem_obj;
	request_t *r = http->request;

	if (r->store_url) {
		if (mem->store_url == NULL && mem->url == NULL) {
			debug(33, 1) ("clientCacheHit: request has store_url '%s'; mem has no url or store_url!\n",
			    r->store_url);
			return 0;
		}
		if (mem->store_url && strcmp(r->store_url, mem->store_url) != 0) {
			debug(33, 1) ("clientCacheHit: request has store_url '%s'; mem object in hit has mis-matched store_url '%s'!\n",
			    r->store_url, mem->store_url);
		    return 0;
		}
		if (mem->store_url == NULL && mem->url && strcmp(r->store_url, mem->url) != 0) {
			debug(33, 1) ("clientCacheHit: request has store_url '%s'; mem object in hit has mis-matched url '%s'!\n",
			    r->store_url, mem->url);
		    return 0;
		}
	} else {			/* no store URL in request */
		if (mem->store_url == NULL && mem->url == NULL) {
			debug(33, 1) ("clientCacheHit: request has url '%s'; mem has no url or store_url!\n",
			    urlCanonical(r));
			return 0;
		}
		/* We currently don't enforce that memObjects with storeurl's -require- a request with a storeurl */
		if (strcmp(mem->url, urlCanonical(r)) != 0) {
			debug(33, 1) ("clientCacheHit: (store url '%s'); URL mismatch '%s' != '%s'?\n",
			    r->store_url, e->mem_obj->url, urlCanonical(r));
			return 0;
		}
	}
	return 1;
}

/*
 * clientCacheHit should only be called until the HTTP reply headers
 * have been parsed.  Normally this should be a single call, but
 * it might take more than one.  As soon as we have the headers,
 * we hand off to clientSendMoreData, clientProcessExpired, or
 * clientProcessMiss.
 */
void
clientCacheHit(void *data, HttpReply * rep)
{
    clientHttpRequest *http = data;
    StoreEntry *e = http->entry;
    MemObject *mem;
    request_t *r = http->request;
    int is_modified = -1;
    int stale;
    http->flags.hit = 0;
    if (http->entry == NULL) {
	debug(33, 3) ("clientCacheHit: request aborted\n");
	return;
    } else if (!rep) {
	/* swap in failure */
	debug(33, 3) ("clientCacheHit: swapin failure for %s\n", http->uri);
	http->log_type = LOG_TCP_SWAPFAIL_MISS;
	clientProcessMiss(http);
	return;
    } else if (EBIT_TEST(e->flags, ENTRY_ABORTED)) {
	/* aborted object */
	debug(33, 3) ("clientCacheHit: hit an aborted object %s\n", http->uri);
	http->log_type = LOG_TCP_SWAPFAIL_MISS;
	clientProcessMiss(http);
	return;
    }
    mem = e->mem_obj;
    debug(33, 3) ("clientCacheHit: %s = %d\n", http->uri, rep->sline.status);

    /* Make sure the request URL matches the object URL. */
    /* Take the store URL into account! */
    if (! clientCheckUrlIsValid(http)) {
        clientProcessMiss(http);
        return;
    }
    if (r->flags.collapsed && EBIT_TEST(e->flags, RELEASE_REQUEST)) {
	/* collapsed_forwarding, but the joined request is not good
	 * to be cached..
	 */
	clientProcessMiss(http);
	return;
    }
    /*
     * Got the headers, now grok them
     */
    assert(http->log_type == LOG_TCP_HIT);
    switch (varyEvaluateMatch(e, r)) {
    case VARY_NONE:
	/* No variance detected. Continue as normal */
	break;
    case VARY_MATCH:
	/* This is the correct entity for this request. Continue */
	debug(33, 2) ("clientCacheHit: Vary MATCH!\n");
	break;
    case VARY_OTHER:
	{
	    /* This is not the correct entity for this request. We need
	     * to requery the cache.
	     */
	    store_client *sc = http->sc;
	    http->entry = NULL;	/* saved in e */
	    /* Warning: storeClientUnregister may abort the object so we must
	     * call storeLocateVary before unregistering, and
	     * storeLocateVary may complete immediately so we cannot
	     * rely on the http structure for this...
	     */
	    http->sc = NULL;
	    storeLocateVary(e, e->mem_obj->reply->hdr_sz, r->vary_headers, r->vary_encoding, clientProcessVary, http);
	    storeClientUnregister(sc, e, http);
	    storeUnlockObject(e);
	    /* Note: varyEvalyateMatch updates the request with vary information
	     * so we only get here once. (it also takes care of cancelling loops)
	     */
	    debug(33, 2) ("clientCacheHit: Vary detected!\n");
	    return;
	}
    case VARY_RESTART:
	/* Used on collapsed requests when the main request wasn't
	 * compatible. Resart processing from the beginning.
	 */
	safe_free(r->vary_hdr);
	safe_free(r->vary_headers);
	clientProcessRequest(http);
	return;
    case VARY_CANCEL:
	/* varyEvaluateMatch found a object loop. Process as miss */
	debug(33, 1) ("clientCacheHit: Vary object loop!\n");
	storeClientUnregister(http->sc, e, http);
	http->sc = NULL;
	clientProcessMiss(http);
	return;
    }
    if (r->method->code == METHOD_PURGE) {
	http->entry = NULL;
	storeClientUnregister(http->sc, e, http);
	http->sc = NULL;
	storeUnlockObject(e);
	clientPurgeRequest(http);
	return;
    }
    http->flags.hit = 1;
    if (EBIT_TEST(e->flags, ENTRY_NEGCACHED)) {
	if (checkNegativeHit(e)
#if HTTP_VIOLATIONS
	    && !r->flags.nocache_hack
#endif
	    ) {
	    http->log_type = LOG_TCP_NEGATIVE_HIT;
	    clientSendHeaders(data, rep);
	} else {
	    http->log_type = LOG_TCP_MISS;
	    clientProcessMiss(http);
	}
	return;
    }
    if (httpHeaderHas(&r->header, HDR_IF_MATCH)) {
	const char *rep_etag = httpHeaderGetStr(&e->mem_obj->reply->header, HDR_ETAG);
	int has_etag = 0;
	if (rep_etag) {
	    String req_etags = httpHeaderGetList(&http->request->header, HDR_IF_MATCH);
	    has_etag = strListIsMember(&req_etags, rep_etag, ',');
	    stringClean(&req_etags);
	}
	if (!has_etag) {
	    /* The entity tags does not match. This cannot be a hit for this object.
	     * Query the origin to see what should be done.
	     */
	    http->log_type = LOG_TCP_MISS;
	    clientProcessMiss(http);
	    return;
	}
    }
    if (httpHeaderHas(&r->header, HDR_IF_NONE_MATCH)) {
	String req_etags;
	const char *rep_etag = httpHeaderGetStr(&e->mem_obj->reply->header, HDR_ETAG);
	int has_etag;
	if (mem->reply->sline.status != HTTP_OK) {
	    debug(33, 4) ("clientCacheHit: Reply code %d != 200\n",
		mem->reply->sline.status);
	    http->log_type = LOG_TCP_MISS;
	    clientProcessMiss(http);
	    return;
	}
	if (rep_etag) {
	    req_etags = httpHeaderGetList(&http->request->header, HDR_IF_NONE_MATCH);
	    has_etag = strListIsMember(&req_etags, rep_etag, ',');
	    stringClean(&req_etags);
	    if (has_etag) {
		debug(33, 4) ("clientCacheHit: If-None-Match matches\n");
		if (is_modified == -1)
		    is_modified = 0;
	    } else {
		debug(33, 4) ("clientCacheHit: If-None-Match mismatch\n");
		is_modified = 1;
	    }
	}
    }
    if (r->flags.ims && mem->reply->sline.status == HTTP_OK) {
	if (modifiedSince(e, http->request)) {
	    debug(33, 4) ("clientCacheHit: If-Modified-Since modified\n");
	    is_modified = 1;
	} else {
	    debug(33, 4) ("clientCacheHit: If-Modified-Since not modified\n");
	    if (is_modified == -1)
		is_modified = 0;
	}
    }

    /*
     * There's the possibility that a cached redirect will refer to the same URL
     * in some circumstances where the request URL is being rewritten (eg storeurl)
     * where the redirect is not explicitly as uncachable. 
     * Deny looping here and do not cache the response.
     */
    /*
     * XXX strcmp() sucks but the strings are both C strings. Look at String'ifying it
     * XXX soon!
     */
	if (mem->reply->sline.status >= 300 && mem->reply->sline.status < 400) {
	if (httpHeaderHas(&e->mem_obj->reply->header, HDR_LOCATION))
	if (!strcmp(http->uri,httpHeaderGetStr(&e->mem_obj->reply->header, HDR_LOCATION))) {
		debug(33, 2) ("clientCacheHit: Redirect Loop Detected: %s\n",http->uri);
		http->log_type = LOG_TCP_MISS;
		clientProcessMiss(http);
			return;
	}
	}
    stale = refreshCheckHTTPStale(e, r);
    debug(33, 2) ("clientCacheHit: refreshCheckHTTPStale returned %d\n", stale);
    if (stale == 0) {
	debug(33, 2) ("clientCacheHit: HIT\n");
    } else if (stale == -1 && Config.refresh_stale_window > 0 && e->mem_obj->refresh_timestamp + Config.refresh_stale_window > squid_curtime) {
	debug(33, 2) ("clientCacheHit: refresh_stale HIT\n");
	http->log_type = LOG_TCP_STALE_HIT;
	stale = 0;
    } else if (stale == -2 && e->mem_obj->refresh_timestamp + e->mem_obj->stale_while_revalidate >= squid_curtime) {
	debug(33, 2) ("clientCacheHit: stale-while-revalidate HIT\n");
	http->log_type = LOG_TCP_STALE_HIT;
	stale = 0;
    } else if (stale && http->flags.internal) {
	debug(33, 2) ("clientCacheHit: internal HIT\n");
	stale = 0;
    } else if (stale && Config.onoff.offline) {
	debug(33, 2) ("clientCacheHit: offline HIT\n");
	http->log_type = LOG_TCP_OFFLINE_HIT;
	stale = 0;
    } else if (stale == -2 && !clientOnlyIfCached(http)) {
	debug(33, 2) ("clientCacheHit: stale-while-revalidate needs revalidation\n");
	clientAsyncRefresh(http);
	http->log_type = LOG_TCP_STALE_HIT;
	stale = 0;
    }
    http->is_modified = is_modified;
    if (stale) {
	debug(33, 5) ("clientCacheHit: in refreshCheck() block\n");
	/*
	 * We hold a stale copy; it needs to be validated
	 */
	/*
	 * The 'need_validation' flag is used to prevent forwarding
	 * loops between siblings.  If our copy of the object is stale,
	 * then we should probably only use parents for the validation
	 * request.  Otherwise two siblings could generate a loop if
	 * both have a stale version of the object.
	 */
	r->flags.need_validation = 1;
	if (r->flags.nocache) {
	    /*
	     * This did not match a refresh pattern that overrides no-cache
	     * we should honour the client no-cache header.
	     */
	    http->log_type = LOG_TCP_CLIENT_REFRESH_MISS;
	    clientProcessMiss(http);
	    return;
	}
	clientRefreshCheck(http);
	return;
    }
    clientProcessHit(http);
}

void
clientProcessHit(clientHttpRequest * http)
{
    int is_modified = http->is_modified;
    StoreEntry *e = http->entry;

    if (is_modified == 0) {
	time_t timestamp = e->timestamp;
	MemBuf mb = httpPacked304Reply(e->mem_obj->reply, http->conn->port->http11);
	http->log_type = LOG_TCP_IMS_HIT;
	storeClientUnregister(http->sc, e, http);
	http->sc = NULL;
	storeUnlockObject(e);
	e = clientCreateStoreEntry(http, http->request->method, null_request_flags);
	/*
	 * Copy timestamp from the original entry so the 304
	 * reply has a meaningful Age: header.
	 */
	http->entry = e;
	httpReplyParse(e->mem_obj->reply, mb.buf, mb.size);
	storeTimestampsSet(e);
	e->timestamp = timestamp;
	storeAppend(e, mb.buf, mb.size);
	memBufClean(&mb);
	storeComplete(e);
	return;
    }
    /*
     * plain ol' cache hit
     */
    if (EBIT_TEST(e->flags, REFRESH_FAILURE))
	http->log_type = LOG_TCP_NEGATIVE_HIT;
    if (e->store_status != STORE_OK)
	http->log_type = LOG_TCP_MISS;
    else if (http->log_type == LOG_TCP_HIT && e->mem_status == IN_MEMORY)
	http->log_type = LOG_TCP_MEM_HIT;
    clientSendHeaders(http, e->mem_obj->reply);
}

/*
 * Calculates the maximum size allowed for an HTTP response
 */
static void
clientMaxBodySize(request_t * request, clientHttpRequest * http, HttpReply * reply)
{
    body_size *bs;
    aclCheck_t *checklist;
    if (http->log_type == LOG_TCP_DENIED)
	return;
    bs = (body_size *) Config.ReplyBodySize.head;
    while (bs) {
	checklist = clientAclChecklistCreate(bs->access_list, http);
	checklist->reply = reply;
	if (aclCheckFast(bs->access_list, checklist)) {
	    /* deny - skip this entry */
	    bs = (body_size *) bs->node.next;
	} else {
	    /* Allow - use this entry */
	    http->maxBodySize = bs->maxsize;
	    bs = NULL;
	    debug(58, 3) ("clientMaxBodySize: Setting maxBodySize to %ld\n", (long int) http->maxBodySize);
	}
	aclChecklistFree(checklist);
    }
}

#if DELAY_POOLS
/*
 * Calculates the delay maximum size allowed for an HTTP response
 */
static void
clientDelayMaxBodySize(request_t * request, clientHttpRequest * http, HttpReply * reply)
{
    delay_body_size *dbs;
    aclCheck_t *checklist;
    if (http->log_type == LOG_TCP_DENIED)
	return;
    dbs = (delay_body_size *) Config.DelayBodySize.head;
    while (dbs) {
	checklist = clientAclChecklistCreate(dbs->access_list, http);

	checklist->reply = reply;
	if (1 != aclCheckFast(dbs->access_list, checklist)) {
	    /* deny - skip this entry */
	    dbs = (delay_body_size *) dbs->node.next;
	} else {
	    /* Allow - use this entry */
	    http->delayMaxBodySize = dbs->maxsize;
	    http->delayAssignedPool = dbs->pool;
	    dbs = NULL;
	    debug(58, 3) ("clientDelayMaxBodySize: Setting delayMaxBodySize to %ld\n",
		(long int) http->delayMaxBodySize);
	}
	aclChecklistFree(checklist);
    }
}

#endif

static int
clientReplyBodyTooLarge(clientHttpRequest * http, squid_off_t clen)
{
    if (0 == http->maxBodySize)
	return 0;		/* disabled */
    if (clen < 0)
	return 0;		/* unknown */
    if (clen > http->maxBodySize)
	return 1;		/* too large */
    return 0;
}

#if DELAY_POOLS
static int
clientDelayBodyTooLarge(clientHttpRequest * http, squid_off_t clen)
{
    if (0 == http->delayMaxBodySize)
	return 0;		/* disabled */
    if (clen < 0)
	return 0;		/* unknown */
    if (clen > http->delayMaxBodySize)
	return 1;		/* too large */
    return 0;
}
#endif

/*
 * Calculate the maximum size to delay requests for the HTTP request body
 */
static int
clientMaxRequestBodyDelayForwardSize(request_t * request, clientHttpRequest * http)
{
    body_size *bs;
    aclCheck_t *checklist;

    /* Already calculated? use it */
    if (http->maxRequestBodyDelayForwardSize)
	return http->maxRequestBodyDelayForwardSize;

    /* Calculate it */
    bs = (body_size *) Config.RequestBodyDelayForwardSize.head;
    while (bs) {
	checklist = clientAclChecklistCreate(bs->access_list, http);
	if (aclCheckFast(bs->access_list, checklist) != 1) {
	    /* deny - skip this entry */
	    bs = (body_size *) bs->node.next;
	} else {
	    /* Allow - use this entry */
	    http->maxRequestBodyDelayForwardSize = bs->maxsize;
	    bs = NULL;
	    debug(33, 2) ("clientMaxRequestBodyDelayForwardSize: Setting maxRequestBodyDelayForwardSize to %ld\n", (long int) http->maxRequestBodyDelayForwardSize);
	}
	aclChecklistFree(checklist);
    }
    return http->maxRequestBodyDelayForwardSize;
}

/* Responses with no body will not have a content-type header, 
 * which breaks the rep_mime_type acl, which
 * coincidentally, is the most common acl for reply access lists.
 * A better long term fix for this is to allow acl matchs on the various
 * status codes, and then supply a default ruleset that puts these 
 * codes before any user defines access entries. That way the user 
 * can choose to block these responses where appropriate, but won't get
 * mysterious breakages.
 */
static int
clientAlwaysAllowResponse(http_status sline)
{
    switch (sline) {
    case HTTP_CONTINUE:
    case HTTP_SWITCHING_PROTOCOLS:
    case HTTP_PROCESSING:
    case HTTP_NO_CONTENT:
    case HTTP_NOT_MODIFIED:
	return 1;
	/* unreached */
	break;
    default:
	return 0;
    }
}

static void clientHttpReplyAccessCheckDone(int answer, void *data);
static void clientCheckErrorMap(clientHttpRequest * http);
static void clientCheckHeaderDone(clientHttpRequest * http);

static void
clientSetClientTOS(ConnStateData *conn, clientHttpRequest *http)
{
    int fd = conn->fd;

    if (Config.zph_mode != ZPH_OFF) {
	int tos = 0;

	if (!isTcpHit(http->log_type))
	    tos = 0;
	else if (Config.zph_sibling && http->request->hier.code == SIBLING_HIT)		/* sibling hit */
	    tos = Config.zph_sibling;
	else if (Config.zph_parent && http->request->hier.code == PARENT_HIT)	/* parent hit */
	    tos = Config.zph_parent;
	else if (Config.zph_local)
	    tos = Config.zph_local;
	if (conn->tos_priority != tos) {
	    conn->tos_priority = tos;
	    switch (Config.zph_mode) {
	    case ZPH_OFF:
		break;
	    case ZPH_TOS:
		commSetTos(fd, tos);
		http->client_tos = tos;
		break;
	    case ZPH_PRIORITY:
		commSetSocketPriority(fd, tos);
		break;
	    case ZPH_OPTION:
		{
		    uint16_t value = tos;
		    commSetIPOption(fd, Config.zph_option, &value, sizeof(value));
		}
		break;
	    }
	}
    }
}

/*
 * accepts chunk of a http message in buf, parses prefix, filters headers and
 * such, writes processed message to the client's socket
 */
void
clientSendHeaders(void *data, HttpReply * rep)
{
    //const char *buf = ref.node->data + ref.offset;
    clientHttpRequest *http = data;
    StoreEntry *entry = http->entry;
    ConnStateData *conn = http->conn;
    int fd = conn->fd;
    assert(http->request != NULL);
    dlinkDelete(&http->active, &ClientActiveRequests);
    dlinkAdd(http, &http->active, &ClientActiveRequests);
    debug(33, 5) ("clientSendHeaders: FD %d '%s'\n", fd, storeUrl(entry));
    assert(conn->reqs.head != NULL);
    if (DLINK_HEAD(conn->reqs) != http) {
	/* there is another object in progress, defer this one */
	debug(33, 2) ("clientSendHeaders: Deferring %s\n", storeUrl(entry));
	return;
    } else if (http->request->flags.reset_tcp) {
	comm_reset_close(fd);
	return;
    } else if (!rep) {
	/* call clientWriteComplete so the client socket gets closed */
	clientWriteComplete(fd, NULL, 0, COMM_OK, http);
	return;
    }
    assert(http->out.offset == 0);
    clientSetClientTOS(conn, http);

    rep = http->reply = clientCloneReply(http, rep);
    if (!rep) {
	ErrorState *err = errorCon(ERR_INVALID_RESP, HTTP_BAD_GATEWAY, http->orig_request);
	storeClientUnregister(http->sc, http->entry, http);
	http->sc = NULL;
	storeUnlockObject(http->entry);
	http->entry = clientCreateStoreEntry(http, http->request->method,
	    null_request_flags);
	errorAppendEntry(http->entry, err);
	return;
    }
    clientMaxBodySize(http->request, http, rep);
#if DELAY_POOLS
    clientDelayMaxBodySize(http->request, http, rep);
#endif
    if (http->log_type != LOG_TCP_DENIED && clientReplyBodyTooLarge(http, rep->content_length)) {
	ErrorState *err = errorCon(ERR_TOO_BIG, HTTP_FORBIDDEN, http->orig_request);
	storeClientUnregister(http->sc, http->entry, http);
	http->sc = NULL;
	storeUnlockObject(http->entry);
	http->log_type = LOG_TCP_DENIED;
	http->entry = clientCreateStoreEntry(http, http->request->method,
	    null_request_flags);
	errorAppendEntry(http->entry, err);
	return;
    }
    /* 
     * At this point we might have more data in the headers than this silly 4k read.
     * So lets just ignore there being any body data in this particular read
     * (as eventually we won't be issuing a read just to get header data) and issue
     * our next read at the point just after the reply length in rep->hdr_sz.
     * Hopefully this horrible hackery will go away once the store API has changed to
     * seperate entity-data and reply-data. We'll then reinstance the "grab header data
     * and body data, writing them out in one swift hit" logic which I've just disabled.
     * - [ahc]
     */
    http->range_iter.prefix_size = rep->hdr_sz;
    debug(33, 3) ("clientSendHeaders: %d bytes of headers\n", rep->hdr_sz);
    clientHttpLocationRewriteCheck(http);
}

void
clientHttpReplyAccessCheck(clientHttpRequest * http)
{
    aclCheck_t *ch;
    if (Config.accessList.reply && http->log_type != LOG_TCP_DENIED && !clientAlwaysAllowResponse(http->reply->sline.status)) {
	ch = clientAclChecklistCreate(Config.accessList.reply, http);
	ch->reply = http->reply;
	aclNBCheck(ch, clientHttpReplyAccessCheckDone, http);
    } else {
	clientHttpReplyAccessCheckDone(ACCESS_ALLOWED, http);
    }
}

/* Handle error mapping.
 * 
 *   1. Look up if there is a error map for the request
 *   2. Start requesting the error URL
 *   3. When headers are received, create a new reply structure and copy
 *      over the relevant headers (start with the headers from the original
 *      reply, and copy over Content-Length)
 *   4. Make the new reply the current one
 *   5. Detatch from the previous reply
 *   6. Go to clientCheckHeaderDone, as if nothing had happened, but now
 *      fetching from the new reply.
 */
static void
clientHttpReplyAccessCheckDone(int answer, void *data)
{
    clientHttpRequest *http = data;
    debug(33, 2) ("The reply for %s %s is %s, because it matched '%s'\n",
	urlMethodGetConstStr(http->request->method), http->uri,
	answer ? "ALLOWED" : "DENIED",
	AclMatchedName ? AclMatchedName : "NO ACL's");
    if (answer != ACCESS_ALLOWED) {
	ErrorState *err;
	err_type page_id;
	page_id = aclGetDenyInfoPage(&Config.denyInfoList, AclMatchedName, 1);
	if (page_id == ERR_NONE)
	    page_id = ERR_ACCESS_DENIED;
	err = errorCon(page_id, HTTP_FORBIDDEN, http->orig_request);
	storeClientUnregister(http->sc, http->entry, http);
	http->sc = NULL;
	if (http->reply)
	    httpReplyDestroy(http->reply);
	http->reply = NULL;
	storeUnlockObject(http->entry);
	http->log_type = LOG_TCP_DENIED;
	http->entry = clientCreateStoreEntry(http, http->request->method,
	    null_request_flags);
	errorAppendEntry(http->entry, err);
	return;
    }
    clientCheckErrorMap(http);
}

static void
clientCheckErrorMapDone(StoreEntry * e, int body_offset, squid_off_t content_length, void *data)
{
    clientHttpRequest *http = data;
    if (e) {
	/* Get rid of the old request entry */
	storeClientUnregister(http->sc, http->entry, http);
	storeUnlockObject(http->entry);
	/* Attach ourselves to the new request entry */
	http->entry = e;
	storeLockObject(e);
	http->sc = storeClientRegister(http->entry, http);
	/* Adjust the header size */
	http->reply->hdr_sz = body_offset;
	/* Clean up any old body content */
	httpBodyClean(&http->reply->body);
	/* And finally, adjust content-length to the new value */
	httpHeaderDelById(&http->reply->header, HDR_CONTENT_LENGTH);
	if (content_length >= 0) {
	    httpHeaderPutSize(&http->reply->header, HDR_CONTENT_LENGTH, content_length);
	}
	http->reply->content_length = content_length;
    }
    clientCheckHeaderDone(http);
}

static void
clientCheckErrorMap(clientHttpRequest * http)
{
    HttpReply *rep = http->reply;
    if (rep->sline.status < 100 || rep->sline.status >= 400) {
	request_t *request = http->orig_request;
	/* XXX The NULL is meant to pass ACL name, but the ACL name is not
	 * known here (AclMatchedName is no longer valid)
	 */
	if (errorMapStart(Config.errorMapList, request, rep, NULL, clientCheckErrorMapDone, http))
	    return;
    }
    clientCheckHeaderDone(http);
}

static void
clientCheckHeaderDone(clientHttpRequest * http)
{
    HttpReply *rep = http->reply;
    MemBuf mb;
    int send_header = 1;
    /* reset range iterator */
    http->range_iter.pos = HttpHdrRangeInitPos;
    if (http->request->method != NULL) {
	if (http->request->method->code == METHOD_HEAD) {
	    /* do not forward body for HEAD replies */
	    http->flags.done_copying = 1;
	}
    }
    if (http->http_ver.major < 1)
	send_header = 0;
    if (rep->sline.version.major < 1) {
	if (send_header && Config.accessList.upgrade_http09) {
	    aclCheck_t *checklist = clientAclChecklistCreate(Config.accessList.upgrade_http09, http);
	    checklist->reply = rep;
	    if (aclCheckFast(Config.accessList.upgrade_http09, checklist) != 1)
		send_header = 0;
	    aclChecklistFree(checklist);
	}
	httpHeaderDelById(&rep->header, HDR_X_HTTP09_FIRST_LINE);
    }
    /* init mb; put status line and headers  */
    if (send_header) {
	if (http->conn->port->http11) {
	    /* enforce 1.1 reply version */
	    httpBuildVersion(&rep->sline.version, 1, 1);
	} else {
	    /* enforce 1.0 reply version */
	    httpBuildVersion(&rep->sline.version, 1, 0);
	}
	mb = httpReplyPack(rep);
    } else {
	debug(33, 2) ("HTTP/0.9 response, disable everything\n");
	http->request->flags.chunked_response = 0;
	http->request->flags.proxy_keepalive = 0;
	memBufDefInit(&mb);
    }
    if (Config.onoff.log_mime_hdrs) {
	http->al.headers.reply = xmalloc(mb.size + 1);
	xstrncpy(http->al.headers.reply, mb.buf, mb.size);
	http->al.headers.reply[mb.size] = '\0';
    }
    http->out.offset += rep->hdr_sz;
#if HEADERS_LOG
    headersLog(0, 0, http->request->method, rep);
#endif
    /* append body if any */
    if (http->request->range) {
	/* Only GET requests should have ranges */
	assert(http->request->method != NULL);
	assert(http->request->method->code == METHOD_GET);
	/* clientPackMoreRanges() updates http->out.offset */
	/* force the end of the transfer if we are done */
	if (!clientPackMoreRanges(http, "", 0, &mb))
	    http->flags.done_copying = 1;
    }
    /* write headers and initial body */
    if (mb.size > 0) {
	comm_write_mbuf(http->conn->fd, mb, clientWriteComplete, http);
    } else {
	memBufClean(&mb);
	storeClientRef(http->sc, http->entry,
	    http->out.offset,
	    http->out.offset,
	    SM_PAGE_SIZE,
	    clientSendMoreData,
	    http);
    }
}


/*
 * accepts chunk of a http message in buf, parses prefix, filters headers and
 * such, writes processed message to the client's socket
 */
static void
clientSendMoreData(void *data, mem_node_ref ref, ssize_t size)
{
    const char *buf = NULL;
    clientHttpRequest *http = data;
    StoreEntry *entry = http->entry;
    ConnStateData *conn = http->conn;
    int fd = conn->fd;
    MemBuf mb;
    debug(33, 5) ("clientSendMoreData: %s, %d bytes\n", http->uri, (int) size);
    assert(size + ref.offset <= SM_PAGE_SIZE);
    assert(size <= SM_PAGE_SIZE);
    assert(http->request != NULL);
    dlinkDelete(&http->active, &ClientActiveRequests);
    dlinkAdd(http, &http->active, &ClientActiveRequests);
    debug(33, 5) ("clientSendMoreData: FD %d '%s', out.offset=%d \n",
	fd, storeUrl(entry), (int) http->out.offset);
    assert(conn->reqs.head != NULL);
    if (DLINK_HEAD(conn->reqs) != http) {
	/* there is another object in progress, defer this one */
	debug(33, 1) ("clientSendMoreData: Deferring %s\n", storeUrl(entry));
	stmemNodeUnref(&ref);
	return;
    } else if (size < 0) {
	/* call clientWriteComplete so the client socket gets closed */
	clientWriteComplete(fd, NULL, 0, COMM_OK, http);
	stmemNodeUnref(&ref);
	return;
    } else if (size == 0) {
	/* call clientWriteComplete so the client socket gets closed */
	clientWriteComplete(fd, NULL, 0, COMM_OK, http);
	stmemNodeUnref(&ref);
	return;
    }
    assert(ref.node->data);
    buf = ref.node->data + ref.offset;
    if (!http->request->range && !http->request->flags.chunked_response) {
	/* Avoid copying to MemBuf for non-range requests */
	http->out.offset += size;
	/* XXX eww - these refcounting semantics should be better adrian! fix it! */
	http->nr = ref;
	comm_write(fd, buf, size, clientWriteBodyComplete, http, NULL);
	/* NULL because clientWriteBodyComplete frees it */
	return;
    }
    if (http->request->method->code == METHOD_HEAD) {
	/*
	 * If we are here, then store_status == STORE_OK and it
	 * seems we have a HEAD repsponse which is missing the
	 * empty end-of-headers line (home.mira.net, phttpd/0.99.72
	 * does this).  Because clientCloneReply() fails we just
	 * call this reply a body, set the done_copying flag and
	 * continue...
	 */
	http->flags.done_copying = 1;
	/*
	 * And as this is a malformed HTTP reply we cannot keep
	 * the connection persistent
	 */
	http->request->flags.proxy_keepalive = 0;
    }
    /* init mb; put status line and headers if any */
    memBufDefInit(&mb);
    if (http->request->range) {
	/* Only GET requests should have ranges */
	assert(http->request->method->code == METHOD_GET);
	/* clientPackMoreRanges() updates http->out.offset */
	/* force the end of the transfer if we are done */
	if (!clientPackMoreRanges(http, buf, size, &mb))
	    http->flags.done_copying = 1;
    } else {
	http->out.offset += size;
	memBufAppend(&mb, buf, size);
    }
    /* write body */
    if (http->request->flags.chunked_response) {
	char header[32];
	size_t header_size;
	header_size = snprintf(header, sizeof(header), "%x\r\n", mb.size);
	memBufAppend(&mb, "\r\n", 2);
	comm_write_mbuf_header(fd, mb, header, header_size, clientWriteComplete, http);
    } else {
	comm_write_mbuf(fd, mb, clientWriteComplete, http);
    }
    stmemNodeUnref(&ref);
}

/*
 * clientWriteBodyComplete is called for buffers
 * written directly to the client socket, versus copying to a MemBuf
 * and going through comm_write_mbuf.  Most non-range responses after
 * the headers probably go through here.
 */
static void
clientWriteBodyComplete(int fd, char *buf, size_t size, int errflag, void *data)
{
    clientHttpRequest *http = data;
    /*
     * NOTE: clientWriteComplete doesn't currently use its "buf"
     * (second) argument, so we pass in NULL.
     */
    stmemNodeUnref(&http->nr);
    clientWriteComplete(fd, NULL, size, errflag, data);
}

void
clientKeepaliveNextRequest(clientHttpRequest * http)
{
    ConnStateData *conn = http->conn;
    StoreEntry *entry;
    debug(33, 3) ("clientKeepaliveNextRequest: FD %d\n", conn->fd);
    conn->defer.until = 0;	/* Kick it to read a new request */
    httpRequestFree(http);
    if (conn->pinning.pinned && conn->pinning.fd == -1) {
	debug(33, 2) ("clientKeepaliveNextRequest: FD %d Connection was pinned but server side gone. Terminating client connection\n", conn->fd);
	comm_close(conn->fd);
	return;
    }
    http = NULL;
    if (conn->reqs.head != NULL) {
	http = DLINK_HEAD(conn->reqs);
    }
    if (http == NULL) {
	debug(33, 5) ("clientKeepaliveNextRequest: FD %d reading next req\n",
	    conn->fd);
	fd_note_static(conn->fd, "Waiting for next request");
	/*
	 * Set the timeout BEFORE calling clientReadRequest().
	 */
	commSetTimeout(conn->fd, Config.Timeout.persistent_request, requestTimeout, conn);
	clientReadRequest(conn->fd, conn);	/* Read next request */
	/*
	 * Note, the FD may be closed at this point.
	 */
    } else if ((entry = http->entry) == NULL) {
	/*
	 * this request is in progress, maybe doing an ACL or a redirect,
	 * execution will resume after the operation completes.
	 */
	/* if it was a pipelined CONNECT kick it alive here */
	if (http->request->method->code == METHOD_CONNECT)
	    clientCheckFollowXForwardedFor(http);
    } else {
	debug(33, 2) ("clientKeepaliveNextRequest: FD %d Sending next\n",
	    conn->fd);
	assert(entry);
	if (0 == storeClientCopyPending(http->sc, entry, http)) {
	    if (EBIT_TEST(entry->flags, ENTRY_ABORTED))
		debug(33, 0) ("clientKeepaliveNextRequest: ENTRY_ABORTED\n");
	    storeClientCopyHeaders(http->sc, entry,
		clientSendHeaders,
		http);
	}
    }
}

static void
clientWriteComplete(int fd, char *bufnotused, size_t size, int errflag, void *data)
{
    clientHttpRequest *http = data;
    StoreEntry *entry = http->entry;
    int done;
    http->out.size += size;
    debug(33, 5) ("clientWriteComplete: FD %d, sz %d, err %d, off %" PRINTF_OFF_T ", len %" PRINTF_OFF_T "\n",
	fd, (int) size, errflag, http->out.offset, entry ? objectLen(entry) : (squid_off_t) 0);
    if (size > 0) {
	kb_incr(&statCounter.client_http.kbytes_out, size);
	if (isTcpHit(http->log_type))
	    kb_incr(&statCounter.client_http.hit_kbytes_out, size);
    }
#if SIZEOF_SQUID_OFF_T <= 4
    if (http->out.size > 0x7FFF0000) {
	debug(33, 1) ("WARNING: closing FD %d to prevent counter overflow\n", fd);
	debug(33, 1) ("\tclient %s\n", inet_ntoa(http->conn->peer.sin_addr));
	debug(33, 1) ("\treceived %d bytes\n", (int) http->out.size);
	debug(33, 1) ("\tURI %s\n", http->uri);
	comm_close(fd);
    } else
#endif
#if SIZEOF_SQUID_OFF_T <= 4
    if (http->out.offset > 0x7FFF0000) {
	debug(33, 1) ("WARNING: closing FD %d to prevent counter overflow\n", fd);
	debug(33, 1) ("\tclient %s\n", inet_ntoa(http->conn->peer.sin_addr));
	debug(33, 1) ("\treceived %d bytes (offset %d)\n", (int) http->out.size,
	    (int) http->out.offset);
	debug(33, 1) ("\tURI %s\n", http->uri);
	comm_close(fd);
    } else
#endif
    if (errflag) {
	/*
	 * just close the socket, httpRequestFree will abort if needed
	 */
	comm_close(fd);
    } else if (NULL == entry) {
	comm_close(fd);		/* yuk */
    } else if ((done = clientCheckTransferDone(http)) != 0 || size == 0) {
	debug(33, 5) ("clientWriteComplete: FD %d transfer is DONE\n", fd);
	/* We're finished case */
	if (!done) {
	    debug(33, 5) ("clientWriteComplete: closing, !done\n");
	    comm_close(fd);
	} else if (clientGotNotEnough(http)) {
	    debug(33, 5) ("clientWriteComplete: client didn't get all it expected\n");
	    comm_close(fd);
	} else if (EBIT_TEST(http->entry->flags, ENTRY_ABORTED)) {
	    debug(33, 5) ("clientWriteComplete: aborted object\n");
	    comm_close(fd);
	} else if (http->request->flags.chunked_response) {
	    /* Finish chunked transfer encoding */
	    http->request->flags.chunked_response = 0;	/* no longer chunking */
	    comm_write(http->conn->fd, "0\r\n\r\n", 5, clientWriteComplete, http, NULL);
	} else if (http->request->body_reader == clientReadBody) {
	    debug(33, 5) ("clientWriteComplete: closing, but first we need to read the rest of the request\n");
	    /* XXX We assumes the reply does fit in the TCP transmit window.
	     * If not the connection may stall while sending the reply
	     * (before reaching here) if the client does not try to read the
	     * response while sending the request body. As of yet we have
	     * not received any complaints indicating this may be an issue.
	     */
	    clientEatRequestBody(http);
	} else if (http->request->flags.proxy_keepalive) {
	    debug(33, 5) ("clientWriteComplete: FD %d Keeping Alive\n", fd);
	    clientKeepaliveNextRequest(http);
	} else {
	    comm_close(fd);
	}
    } else if (clientReplyBodyTooLarge(http, http->out.offset - 4096)) {
	/* 4096 is a margin for the HTTP headers included in out.offset */
	comm_close(fd);
    } else {
#if DELAY_POOLS
	debug(33, 5) ("clientWriteComplete : Normal\n");
	if (clientDelayBodyTooLarge(http, http->out.offset - 4096)) {
	    debug(33, 5) ("clientWriteComplete: we should put this into the pool: DelayId=%i\n",
		http->sc->delay_id);
	    delayUnregisterDelayIdPtr(&http->sc->delay_id);
	    delaySetStoreClient(http->sc, delayPoolClient(http->delayAssignedPool,
		    (in_addr_t) http->conn->peer.sin_addr.s_addr));
	}
#endif
	/* More data will be coming from primary server; register with 
	 * storage manager. */
	debug(33, 3) ("clientWriteComplete: copying from offset %d\n", (int) http->out.offset);
	storeClientRef(http->sc, entry,
	    http->out.offset,
	    http->out.offset,
	    SM_PAGE_SIZE,
	    clientSendMoreData,
	    http);
    }
}

/*
 * client issued a request with an only-if-cached cache-control directive;
 * we did not find a cached object that can be returned without
 *     contacting other servers;
 * respond with a 504 (Gateway Timeout) as suggested in [RFC 2068]
 */
void
clientProcessOnlyIfCachedMiss(clientHttpRequest * http)
{
    char *url = http->uri;
    request_t *r = http->request;
    ErrorState *err = NULL;
    http->flags.hit = 0;
    debug(33, 4) ("clientProcessOnlyIfCachedMiss: '%s %s'\n",
	urlMethodGetConstStr(r->method), url);
    http->al.http.code = HTTP_GATEWAY_TIMEOUT;
    err = errorCon(ERR_ONLY_IF_CACHED_MISS, HTTP_GATEWAY_TIMEOUT, http->orig_request);
    if (http->entry) {
	storeClientUnregister(http->sc, http->entry, http);
	http->sc = NULL;
	storeUnlockObject(http->entry);
    }
    http->entry = clientCreateStoreEntry(http, r->method, null_request_flags);
    errorAppendEntry(http->entry, err);
}

/*
 * clientProcessRequest2() encapsulates some of the final request caching
 * logic.
 *
 * This is all very dirty and not at all documented; it's quite suprising
 * it all holds together.
 *
 * + If the request is deemed to be cachable, it does a storeEntry lookup
 * + If the request has no-cache set, it invalidates ipcache entries
 * + If the object doesn't exist, it checks some etag processing logic
 *   and then finishes processing
 * + There's a "too complex ranges!" check there which forces a miss
 *
 * + If LOG_TCP_HIT is returned, it sets http->e to be the cache StoreEntry.
 * + If LOG_TCP_MISS is returned, http->e is forcibly set to NULL.
 *
 * I am guessing that http->e is already NULL at this point as StoreEntry
 * references may be refcounted.
 */
static log_type
clientProcessRequest2(clientHttpRequest * http)
{
    request_t *r = http->request;
    StoreEntry *e;
    if (r->flags.cachable || r->flags.internal)
	e = http->entry = storeGetPublicByRequest(r);
    else
	e = http->entry = NULL;
    /* Release IP-cache entries on reload */
    if (r->flags.nocache) {
	ipcacheInvalidateNegative(r->host);
    }
#if HTTP_VIOLATIONS
    else if (r->flags.nocache_hack) {
	ipcacheInvalidateNegative(r->host);
    }
#endif /* HTTP_VIOLATIONS */
#if USE_CACHE_DIGESTS
    http->lookup_type = e ? "HIT" : "MISS";
#endif
    if (NULL == e) {
	/* this object isn't in the cache */
	debug(33, 3) ("clientProcessRequest2: storeGet() MISS\n");
	if (r->vary) {
	    if (r->done_etag) {
		debug(33, 2) ("clientProcessRequest2: ETag loop\n");
	    } else if (r->etags) {
		debug(33, 2) ("clientProcessRequest2: ETag miss\n");
		r->etags = NULL;
	    } else if (r->vary->etags.count > 0) {
		r->etags = &r->vary->etags;
	    }
	}
	return LOG_TCP_MISS;
    }
    if (Config.onoff.offline) {
	debug(33, 3) ("clientProcessRequest2: offline HIT\n");
	http->entry = e;
	return LOG_TCP_HIT;
    }
    if (http->redirect.status) {
	/* force this to be a miss */
	http->entry = NULL;
	return LOG_TCP_MISS;
    }
    if (!storeEntryValidToSend(e)) {
	debug(33, 3) ("clientProcessRequest2: !storeEntryValidToSend MISS\n");
	http->entry = NULL;
	return LOG_TCP_MISS;
    }
    if (EBIT_TEST(e->flags, KEY_EARLY_PUBLIC)) {
	r->flags.collapsed = 1;	/* Don't trust the store entry */
    }
    if (EBIT_TEST(e->flags, ENTRY_SPECIAL)) {
	/* Special entries are always hits, no matter what the client says */
	debug(33, 3) ("clientProcessRequest2: ENTRY_SPECIAL HIT\n");
	http->entry = e;
	return LOG_TCP_HIT;
    }
    if (r->flags.nocache) {
	debug(33, 3) ("clientProcessRequest2: no-cache REFRESH MISS\n");
	http->entry = NULL;
	return LOG_TCP_CLIENT_REFRESH_MISS;
    }
    if (NULL == r->range) {
	(void) 0;
    } else if (httpHdrRangeWillBeComplex(r->range)) {
	/*
	 * Some clients break if we return "200 OK" for a Range
	 * request.  We would have to return "200 OK" for a _complex_
	 * Range request that is also a HIT. Thus, let's prevent HITs
	 * on complex Range requests
	 */
	debug(33, 3) ("clientProcessRequest2: complex range MISS\n");
	http->entry = NULL;
	return LOG_TCP_MISS;
    } else if (clientCheckRangeForceMiss(e, r->range)) {
	debug(33, 3) ("clientProcessRequest2: forcing miss due to range_offset_limit\n");
	http->entry = NULL;
	return LOG_TCP_MISS;
    }
    debug(33, 3) ("clientProcessRequest2: default HIT\n");
    http->entry = e;
    return LOG_TCP_HIT;
}

/*!
 * @function
 * 	clientProcessRequest
 * @abstract
 *	Begin processing a fully validated request
 * @discussion
 *	This function begins the processing chain of a request and determines
 *	whether to begin forwarding upstream or whether the object already
 *	exists in some form.
 *
 *	It handles CONNECT, PURGE and TRACE itself. The rest of the methods
 *	are punted to the forwarding/store layer for handling.
 *
 *	The call to clientProcessRequest2() actually does the initial cache
 *	lookup! The function call itself is rather misleading.
 *
 *	If there's no StoreEntry associated with the object, the request is
 *	punted to clientProcessMiss() for further handling.
 *
 *	If there's a StoreEntry, then some previous processing has performed
 *	the client lookup and successfully found an object to attach to.
 *	In this case, a store client is created and registered and the first
 *	copy of the object reply is queued, which will eventually result in
 *	the request being made if required, and the reply being fed from
 *	the store via either the forwarding or the cache layer.
 *
 * @param	http	clientHttpRequest to begin processing
 */
void
clientProcessRequest(clientHttpRequest * http)
{
    char *url = http->uri;
    request_t *r = http->request;
    HttpReply *rep;
    debug(33, 4) ("clientProcessRequest: %s '%s'\n", urlMethodGetConstStr(r->method), url);
    r->flags.collapsed = 0;
    if (httpHeaderHas(&r->header, HDR_EXPECT)) {
	int ignore = 0;
	if (Config.onoff.ignore_expect_100) {
	    String expect = httpHeaderGetList(&r->header, HDR_EXPECT);
	    if (strCaseCmp(expect, "100-continue") == 0)
		ignore = 1;
	    stringClean(&expect);
	}
	if (!ignore) {
	    ErrorState *err = errorCon(ERR_INVALID_REQ, HTTP_EXPECTATION_FAILED, r);
	    http->log_type = LOG_TCP_MISS;
	    http->entry = clientCreateStoreEntry(http, http->request->method, null_request_flags);
	    errorAppendEntry(http->entry, err);
	    return;
	}
    }
    if (r->method->code == METHOD_CONNECT && !http->redirect.status) {
	http->log_type = LOG_TCP_MISS;
#if USE_SSL && SSL_CONNECT_INTERCEPT
	if (Config.Sockaddr.https) {
	    static const char ok[] = "HTTP/1.0 200 Established\r\n\r\n";
	    write(http->conn->fd, ok, strlen(ok));
	    httpsAcceptSSL(http->conn, Config.Sockaddr.https->sslContext);
	    httpRequestFree(http);
	} else
#endif
	    sslStart(http, &http->out.size, &http->al.http.code);
	return;
    } else if (r->method->code == METHOD_PURGE) {
	clientPurgeRequest(http);
	return;
    } else if (r->method->code == METHOD_TRACE) {
	if (r->max_forwards == 0) {
	    http->log_type = LOG_TCP_HIT;
	    http->entry = clientCreateStoreEntry(http, r->method, null_request_flags);
	    storeReleaseRequest(http->entry);
	    storeBuffer(http->entry);
	    rep = httpReplyCreate();
	    httpReplySetHeaders(rep, HTTP_OK, NULL, "text/plain", httpRequestPrefixLen(r), -1, squid_curtime);
	    httpReplySwapOut(rep, http->entry);
	    httpRequestSwapOut(r, http->entry);
	    storeComplete(http->entry);
	    return;
	}
	/* yes, continue */
	http->log_type = LOG_TCP_MISS;
    } else {
	http->log_type = clientProcessRequest2(http);
    }
    debug(33, 4) ("clientProcessRequest: %s for '%s'\n",
	log_tags[http->log_type],
	http->uri);
    http->out.offset = 0;

    /*
     * http->entry is set by a few places:
     * + by clientProcessRequest2() if the object is in cache;
     * + by a previous call through clientProcessRequest() which has some
     *   existing response to start abusing? I'm not sure about this;
     *
     * If http->entry == NULL then there's no existing object to piggy back
     * onto; so forwarding must begin.
     *
     * If http->entry != NULL then there's an existing object to piggy back
     * onto; so the store client registration occurs and the object is
     * copied in via storeClientCopyHeaders().
     *
     * .. TRACE handling isn't entirely clear either. For a non-terminal
     * TRACE request, it should just bump it upstream or toss out a forwarding
     * error. It doesn't bother trying a cache lookup; it just processes
     * it as a MISS. But there's no explicit setting of http->entry to NULL;
     * I believe it simply expects the only entry point to have it NULL (ie,
     * it hasn't come in via a previously handled request that's being
     * restarted for some reason.)
     */
    if (NULL != http->entry) {
	storeLockObject(http->entry);
	if (http->entry->store_status == STORE_PENDING && http->entry->mem_obj) {
	    if (http->entry->mem_obj->request)
		hierarchyLogEntryCopy(&r->hier, &http->entry->mem_obj->request->hier);
	}
	storeCreateMemObject(http->entry, http->uri);
        urlMethodAssign(&http->entry->mem_obj->method, r->method);
	http->sc = storeClientRegister(http->entry, http);
#if DELAY_POOLS
	delaySetStoreClient(http->sc, delayClient(http));
#endif
	storeClientCopyHeaders(http->sc, http->entry,
	    clientCacheHit,
	    http);
    } else {
	/* MISS CASE, http->log_type is already set! */
	clientProcessMiss(http);
    }
}

static void
clientBeginForwarding(clientHttpRequest * http)
{
    request_t *r = http->request;

    /* XXX should we ensure we only try beginning forwarding once? */
    assert(r->flags.delayed == 0);
    debug(33, 2) ("clientBeginForwarding: %p: begin forwarding request\n", http);
    fwdStart(http->conn->fd, http->entry, r);
}

/*
 * Begin forwarding a request when certain criteria are met.
 *
 * If a request isn't delayed then don't bother.
 * If a request is delayed then check whether enough of the request
 * body has been received. If so, begin forwarding.
 *
 * Return 0 if the request wasn't begun to be forwarded and 1 if it was.
 */
static int
clientCheckBeginForwarding(clientHttpRequest * http)
{
    request_t *r = http->request;
    int bytes_read, bytes_left;
    int bytes_to_delay;

    if (!r->flags.delayed)
	return 0;
    assert(http->conn->body.delayed == 1);
    assert(http->conn->body.delay_http != NULL);

    /* http->conn->in.offset -here- is the amount of request body data that has been read */
    /* (its the size of the buffer; but the buffer -only- contains request body data at this point */

    bytes_read = http->conn->in.offset;
    bytes_left = r->content_length - bytes_read;
    debug(33, 2) ("clientCheckBeginForwarding: request %p: request body size %d; read %d bytes; %d bytes to go\n", r, (int) r->content_length, bytes_read, bytes_left);

    bytes_to_delay = clientMaxRequestBodyDelayForwardSize(r, http);

    /* Forward if all the request body has been read OR bytes_read >= bytes_to_delay */
    /*
     * Handle the case where we've been given slightly more data than we should have -
     * eg, IE7.
     */
    if (bytes_left <= 0 || (bytes_read >= bytes_to_delay)) {
	r->flags.delayed = 0;
	http->conn->body.delayed = 0;
	http->conn->body.delay_http = NULL;
	clientBeginForwarding(http);
	return 1;
    }
    return 0;
}

/*
 * Prepare to fetch the object as it's a cache miss of some kind.
 */
void
clientProcessMiss(clientHttpRequest * http)
{
    char *url = http->uri;
    request_t *r = http->request;
    ErrorState *err = NULL;
    debug(33, 4) ("clientProcessMiss: '%s %s'\n", urlMethodGetConstStr(r->method), url);
    http->flags.hit = 0;
    r->flags.collapsed = 0;
    /*
     * We might have a left-over StoreEntry from a failed cache hit
     * or IMS request.
     */
    if (http->entry) {
	if (EBIT_TEST(http->entry->flags, ENTRY_SPECIAL)) {
	    debug(33, 0) ("clientProcessMiss: miss on a special object (%s).\n", url);
	    debug(33, 0) ("\tlog_type = %s\n", log_tags[http->log_type]);
	    storeEntryDump(http->entry, 1);
	}
	/* touch timestamp for refresh_stale_hit */
	if (http->entry->mem_obj)
	    http->entry->mem_obj->refresh_timestamp = squid_curtime;
	storeClientUnregister(http->sc, http->entry, http);
	http->sc = NULL;
	storeUnlockObject(http->entry);
	http->entry = NULL;
    }
    if (r->method->code == METHOD_PURGE) {
	clientPurgeRequest(http);
	return;
    }
    if (clientOnlyIfCached(http)) {
	clientProcessOnlyIfCachedMiss(http);
	return;
    }
    /*
     * Deny double loops
     */
    if (r->flags.loopdetect_twice) {
	http->al.http.code = HTTP_GATEWAY_TIMEOUT;
	err = errorCon(ERR_CANNOT_FORWARD, HTTP_GATEWAY_TIMEOUT, http->orig_request);
	http->log_type = LOG_TCP_DENIED;
	http->entry = clientCreateStoreEntry(http, r->method, null_request_flags);
	errorAppendEntry(http->entry, err);
	return;
    }
    assert(http->out.offset == 0);
    if (http->redirect.status) {
	HttpReply *rep = httpReplyCreate();
	http->entry = clientCreateStoreEntry(http, r->method, r->flags);
#if LOG_TCP_REDIRECTS
	http->log_type = LOG_TCP_REDIRECT;
#endif
	storeReleaseRequest(http->entry);
	httpRedirectReply(rep, http->redirect.status, http->redirect.location);
	httpReplySwapOut(rep, http->entry);
	storeComplete(http->entry);
	return;
    }
    if (r->etags) {
	clientProcessETag(http);
	return;
    }
    http->entry = clientCreateStoreEntry(http, r->method, r->flags);
    if (Config.onoff.collapsed_forwarding && r->flags.cachable && !r->flags.need_validation && (r->method->code == METHOD_GET || r->method->code == METHOD_HEAD)) {
	http->entry->mem_obj->refresh_timestamp = squid_curtime;
	/* Set the vary object state */
	safe_free(http->entry->mem_obj->vary_headers);
	if (r->vary_headers)
	    http->entry->mem_obj->vary_headers = xstrdup(r->vary_headers);
	safe_free(http->entry->mem_obj->vary_encoding);
	if (strIsNotNull(r->vary_encoding))
	    http->entry->mem_obj->vary_encoding = stringDupToC(&r->vary_encoding);
	http->entry->mem_obj->request = requestLink(r);
	EBIT_SET(http->entry->flags, KEY_EARLY_PUBLIC);
	storeSetPublicKey(http->entry);
    }
    /*
     * Do we need to delay the initial request forwarding for any reason?
     * If so, set the "connection forwarding delayed" flag.
     */
    if (r->content_length > 0) {
	debug(33, 2) ("clientProcessMiss: request %p: time to consider delaying\n", r);
	r->flags.delayed = 1;
	http->conn->body.delayed = 1;
	http->conn->body.delay_http = http;
	clientCheckBeginForwarding(http);
	return;
    }
    clientBeginForwarding(http);
}

static int
clientReadDefer(int fd, void *data)
{
    fde *F = &fd_table[fd];
    ConnStateData *conn = data;
    /* Is there a request body? Defer reading if we've read too far */
    if (conn->body.size_left && !F->flags.socket_eof) {
	if ((!conn->body.delayed) && (conn->in.offset >= conn->in.size - 1)) {
	    commDeferFD(fd);
	    return 1;
	} else {
	    return 0;
	}
    } else {
	if (conn->defer.until > squid_curtime) {
	    /* This is a second resolution timer, so commEpollBackon will 
	     * handle the resume for this defer call */
	    commDeferFD(fd);
	    return 1;
	} else {
	    return 0;
	}
    }
}

static void
clientReadRequest(int fd, void *data)
{
    ConnStateData *conn = data;
    int size;
    fde *F = &fd_table[fd];
    int len = conn->in.size - conn->in.offset - 1;
    int ret;
    debug(33, 4) ("clientReadRequest: FD %d: reading request...\n", fd);
    if (len == 0) {
	/* Grow the request memory area to accomodate for a large request */
	conn->in.buf = memReallocBuf(conn->in.buf, conn->in.size * 2, &conn->in.size);
	debug(33, 2) ("growing request buffer: offset=%ld size=%ld\n",
	    (long) conn->in.offset, (long) conn->in.size);
	len = conn->in.size - conn->in.offset - 1;
    }
    CommStats.syscalls.sock.reads++;
    size = FD_READ_METHOD(fd, conn->in.buf + conn->in.offset, len);
    debug(33, 4) ("clientReadRequest: FD %d: read %d bytes\n", fd, size);
    if (size > 0) {
	fd_bytes(fd, size, FD_READ);
	kb_incr(&statCounter.client_http.kbytes_in, size);
    }
    /*
     * Don't reset the timeout value here.  The timeout value will be
     * set to Config.Timeout.request by httpAccept() and
     * clientWriteComplete(), and should apply to the request as a
     * whole, not individual read() calls.  Plus, it breaks our
     * lame half-close detection
     */
    if (size > 0) {
	conn->in.offset += size;
	conn->in.buf[conn->in.offset] = '\0';	/* Terminate the string */
    } else if (size == 0) {
	if (DLINK_ISEMPTY(conn->reqs) && conn->in.offset == 0) {
	    /* no current or pending requests */
	    debug(33, 4) ("clientReadRequest: FD %d closed\n", fd);
	    comm_close(fd);
	    return;
	} else if (!Config.onoff.half_closed_clients) {
	    /* admin doesn't want to support half-closed client sockets */
	    debug(33, 3) ("clientReadRequest: FD %d aborted (half_closed_clients disabled)\n", fd);
	    comm_close(fd);
	    return;
	}
	/* It might be half-closed, we can't tell */
	debug(33, 5) ("clientReadRequest: FD %d closed?\n", fd);
	F->flags.socket_eof = 1;
	conn->defer.until = squid_curtime + 1;
	conn->defer.n++;
	fd_note_static(fd, "half-closed");
	/* There is one more close check at the end, to detect aborted
	 * (partial) requests. At this point we can't tell if the request
	 * is partial.
	 */
	/* Continue to process previously read data */
    } else if (size < 0) {
	if (!ignoreErrno(errno)) {
	    debug(50, 2) ("clientReadRequest: FD %d: %s\n", fd, xstrerror());
	    comm_close(fd);
	    return;
	} else if (conn->in.offset == 0) {
	    debug(50, 2) ("clientReadRequest: FD %d: no data to process (%s)\n", fd, xstrerror());
	}
	/* Continue to process previously read data */
    }
    cbdataLock(conn);		/* clientProcessBody might pull the connection under our feets */

    /* Check whether we need to kick-start forwarding the request */
    if (conn->in.offset && conn->body.delayed)
	clientCheckBeginForwarding(conn->body.delay_http);

    /* Process request body if any */
    if (conn->in.offset > 0 && conn->body.callback != NULL) {
	clientProcessBody(conn);
	if (!cbdataValid(conn)) {
	    cbdataUnlock(conn);
	    return;
	}
    }
    /* Process next request */
    ret = 0;
    while (cbdataValid(conn) && conn->in.offset > 0 && conn->body.size_left == 0) {
	/* Ret tells us how many bytes was consumed - 0 == didn't consume request, > 0 == consumed, -1 == error, -2 == CONNECT request stole the connection */
	ret = clientTryParseRequest(conn);
	if (ret <= 0)
	    break;
    }				/* while offset > 0 && conn->body.size_left == 0 */
    if (!cbdataValid(conn)) {
	cbdataUnlock(conn);
	return;
    }
    cbdataUnlock(conn);
    /* Check if a half-closed connection was aborted in the middle */
    if (F->flags.socket_eof) {
	if (conn->in.offset != conn->body.size_left) {	/* != 0 when no request body */
	    /* Partial request received. Abort client connection! */
	    debug(33, 3) ("clientReadRequest: FD %d aborted, partial request\n", fd);
	    comm_close(fd);
	    return;
	}
    }
    if (ret >= 0) {
	debug(33, 9) ("clientReadRequest: FD %d: re-registering for another read\n", fd);
	commSetSelect(fd, COMM_SELECT_READ, clientReadRequest, conn, 0);
    }
}

/* general lifetime handler for HTTP requests */
static void
requestTimeout(int fd, void *data)
{
    /*
     * Just close the connection to not confuse browsers
     * using persistent connections. Some browsers opens
     * an connection and then does not use it until much
     * later (presumeably because the request triggering
     * the open has already been completed on another
     * connection)
     */
    debug(33, 3) ("requestTimeout: FD %d: lifetime is expired.\n", fd);
    comm_close(fd);
}

void
clientLifetimeTimeout(int fd, void *data)
{
    clientHttpRequest *http = data;
    ConnStateData *conn = http->conn;
    debug(33, 1) ("WARNING: Closing client %s connection due to lifetime timeout\n",
	inet_ntoa(conn->peer.sin_addr));
    debug(33, 1) ("\t%s\n", http->uri);
    comm_close(fd);
}

static int
httpAcceptDefer(int fd, void *dataunused)
{
    static time_t last_warn = 0;
    if (fdNFree() >= RESERVED_FD)
	return 0;
    if (last_warn + 15 < squid_curtime) {
	debug(33, 0) ("WARNING! Your cache is running out of filedescriptors\n");
	last_warn = squid_curtime;
    }
    commDeferFD(fd);
    return 1;
}

/* Handle a new connection on HTTP socket. */
void
httpAccept(int sock, void *data)
{
    http_port_list *s = data;
    int fd = -1;
    fde *F;
    ConnStateData *connState = NULL;
    sqaddr_t peer;
    sqaddr_t me;
    int max = INCOMING_HTTP_MAX;
#if USE_IDENT
    static aclCheck_t identChecklist;
#endif
    commSetSelect(sock, COMM_SELECT_READ, httpAccept, data, 0);
    while (max-- && !httpAcceptDefer(sock, NULL)) {
        sqinet_init(&peer);
        sqinet_init(&me);
	if ((fd = comm_accept(sock, &peer, &me)) < 0) {
	    if (!ignoreErrno(errno))
		debug(50, 1) ("httpAccept: FD %d: accept failure: %s\n",
		    sock, xstrerror());
            sqinet_done(&peer);
            sqinet_done(&me);
            break;
	}
        if (sqinet_get_family(&peer) != AF_INET) {
            debug(1, 1) ("httpAccept: FD %d: (%s:%d) is not an IPv4 socket!\n", fd, fd_table[fd].ipaddrstr, fd_table[fd].local_port);
            comm_close(fd);
            sqinet_done(&peer);
            sqinet_done(&me);
            break;
       }

	F = &fd_table[fd];
	debug(33, 4) ("httpAccept: FD %d: accepted port %d client %s:%d\n", fd, F->local_port, F->ipaddrstr, F->remote_port);
	fd_note_static(fd, "client http connect");
	connState = connStateCreate(fd, &peer, &me);
        connState->port = s;
        cbdataLock(connState->port);
	if (Config.onoff.log_fqdn)
	    fqdncache_gethostbyaddr(sqinet_get_v4_inaddr(&peer, SQADDR_ASSERT_IS_V4), FQDN_LOOKUP_IF_MISS);
	commSetTimeout(fd, Config.Timeout.request, requestTimeout, connState);
#if USE_IDENT
	identChecklist.src_addr = sqinet_get_v4_inaddr(&peer, SQADDR_ASSERT_IS_V4);
	identChecklist.my_addr = sqinet_get_v4_inaddr(&me, SQADDR_ASSERT_IS_V4);
	identChecklist.my_port = sqinet_get_port(&me);
	if (aclCheckFast(Config.accessList.identLookup, &identChecklist))
	    identStart4(&connState->me, &connState->peer, clientIdentDone, connState);
#endif
	commSetSelect(fd, COMM_SELECT_READ, clientReadRequest, connState, 0);
	commSetDefer(fd, clientReadDefer, connState);
	if (Config.client_socksize > -1)
	    commSetTcpBufferSize(fd, Config.client_socksize);
	if (s->tcp_keepalive.enabled) {
	    commSetTcpKeepalive(fd, s->tcp_keepalive.idle, s->tcp_keepalive.interval, s->tcp_keepalive.timeout);
	}
	clientdbEstablished(sqinet_get_v4_inaddr(&peer, SQADDR_ASSERT_IS_V4), 1);
	incoming_sockets_accepted++;
        sqinet_done(&peer);
        sqinet_done(&me);
    }
}

#if USE_SSL

/* negotiate an SSL connection */
static void
clientNegotiateSSL(int fd, void *data)
{
    ConnStateData *conn = data;
    X509 *client_cert;
    SSL *ssl = fd_table[fd].ssl;
    int ret;

    if ((ret = SSL_accept(ssl)) <= 0) {
	int ssl_error = SSL_get_error(ssl, ret);
	switch (ssl_error) {
	case SSL_ERROR_WANT_READ:
	    commSetSelect(fd, COMM_SELECT_READ, clientNegotiateSSL, conn, 0);
	    return;
	case SSL_ERROR_WANT_WRITE:
	    commSetSelect(fd, COMM_SELECT_WRITE, clientNegotiateSSL, conn, 0);
	    return;
	case SSL_ERROR_SYSCALL:
	    if (ret == 0) {
		debug(83, 2) ("clientNegotiateSSL: Error negotiating SSL connection on FD %d: Aborted by client\n", fd);
		comm_close(fd);
		return;
	    } else {
		int hard = 1;
		if (errno == ECONNRESET)
		    hard = 0;
		debug(83, hard ? 1 : 2) ("clientNegotiateSSL: Error negotiating SSL connection on FD %d: %s (%d)\n",
		    fd, strerror(errno), errno);
		comm_close(fd);
		return;
	    }
	case SSL_ERROR_ZERO_RETURN:
	    debug(83, 1) ("clientNegotiateSSL: Error negotiating SSL connection on FD %d: Closed by client\n", fd);
	    comm_close(fd);
	    return;
	default:
	    debug(83, 1) ("clientNegotiateSSL: Error negotiating SSL connection on FD %d: %s (%d/%d)\n",
		fd, ERR_error_string(ERR_get_error(), NULL), ssl_error, ret);
	    comm_close(fd);
	    return;
	}
	/* NOTREACHED */
    }
    fd_table[fd].read_pending = COMM_PENDING_NOW;
    if (SSL_session_reused(ssl)) {
	debug(83, 2) ("clientNegotiateSSL: Session %p reused on FD %d (%s:%d)\n", SSL_get_session(ssl), fd, fd_table[fd].ipaddrstr, (int) fd_table[fd].remote_port);
    } else {
	if (do_debug(83, 4)) {
	    /* Write out the SSL session details.. actually the call below, but
	     * OpenSSL headers do strange typecasts confusing GCC.. */
	    /* PEM_write_SSL_SESSION(debug_log, SSL_get_session(ssl)); */
#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x00908000L
	    PEM_ASN1_write((i2d_of_void *) i2d_SSL_SESSION, PEM_STRING_SSL_SESSION, debug_log, (char *) SSL_get_session(ssl), NULL, NULL, 0, NULL, NULL);
#else
	    PEM_ASN1_write(i2d_SSL_SESSION, PEM_STRING_SSL_SESSION, debug_log, (char *) SSL_get_session(ssl), NULL, NULL, 0, NULL, NULL);
#endif
	    /* Note: This does not automatically fflush the log file.. */
	}
	debug(83, 2) ("clientNegotiateSSL: New session %p on FD %d (%s:%d)\n", SSL_get_session(ssl), fd, fd_table[fd].ipaddrstr, (int) fd_table[fd].remote_port);
    }
    debug(83, 3) ("clientNegotiateSSL: FD %d negotiated cipher %s\n", fd,
	SSL_get_cipher(ssl));

    client_cert = SSL_get_peer_certificate(ssl);
    if (client_cert != NULL) {
	debug(83, 3) ("clientNegotiateSSL: FD %d client certificate: subject: %s\n", fd,
	    X509_NAME_oneline(X509_get_subject_name(client_cert), 0, 0));

	debug(83, 3) ("clientNegotiateSSL: FD %d client certificate: issuer: %s\n", fd,
	    X509_NAME_oneline(X509_get_issuer_name(client_cert), 0, 0));

	X509_free(client_cert);
    } else {
	debug(83, 5) ("clientNegotiateSSL: FD %d has no certificate.\n", fd);
    }
    clientReadRequest(fd, conn);
}

static void
httpsAcceptSSL(ConnStateData * connState, SSL_CTX * sslContext)
{
    SSL *ssl;
    fde *F;
    int fd = connState->fd;
    if ((ssl = SSL_new(sslContext)) == NULL) {
	int ssl_error = ERR_get_error();
	debug(83, 1) ("httpsAcceptSSL: Error allocating handle: %s\n",
	    ERR_error_string(ssl_error, NULL));
	comm_close(fd);
	return;
    }
    SSL_set_fd(ssl, fd);
    F = &fd_table[fd];
    F->ssl = ssl;
    F->read_method = &ssl_read_method;
    F->write_method = &ssl_write_method;
    debug(50, 5) ("httpsAcceptSSL: FD %d: starting SSL negotiation.\n", fd);
    fd_note_static(fd, "client https connect");

    commSetSelect(fd, COMM_SELECT_READ, clientNegotiateSSL, connState, 0);
    commSetDefer(fd, clientReadDefer, connState);
}

/* handle a new HTTPS connection */
static void
httpsAccept(int sock, void *data)
{
    https_port_list *s = data;
    int fd = -1;
    ConnStateData *connState = NULL;
    sqaddr_t peer;
    sqaddr_t me;
    int max = INCOMING_HTTP_MAX;
#if USE_IDENT
    static aclCheck_t identChecklist;
#endif
    commSetSelect(sock, COMM_SELECT_READ, httpsAccept, s, 0);
    while (max-- && !httpAcceptDefer(sock, NULL)) {
	fde *F;
	sqinet_init(&peer);
	sqinet_init(&me);
	if ((fd = comm_accept(sock, &peer, &me)) < 0) {
	    if (!ignoreErrno(errno))
		debug(50, 1) ("httpsAccept: FD %d: accept failure: %s\n",
		    sock, xstrerror());
            sqinet_done(&peer);
            sqinet_done(&me);
	    break;
	}
        if (sqinet_get_family(&peer) != AF_INET) {
            debug(1, 1) ("httpsAccept: FD %d: (%s:%d) is not an IPv4 socket!\n", fd, fd_table[fd].ipaddrstr, fd_table[fd].local_port);
            comm_close(fd);
            sqinet_done(&peer);
            sqinet_done(&me);
            break;
       }

	F = &fd_table[fd];
	debug(33, 4) ("httpsAccept: FD %d: accepted port %d client %s:%d\n", fd, F->local_port, F->ipaddrstr, F->remote_port);
	connState = connStateCreate(fd, &peer, &me);
	connState->port = (http_port_list *) s;
	cbdataLock(connState->port);
	if (Config.onoff.log_fqdn)
	    fqdncache_gethostbyaddr(connState->peer.sin_addr, FQDN_LOOKUP_IF_MISS);
	commSetTimeout(fd, Config.Timeout.request, requestTimeout, connState);
#if USE_IDENT
	identChecklist.src_addr = sqinet_get_v4_inaddr(&peer, SQADDR_ASSERT_IS_V4);
	identChecklist.my_addr = sqinet_get_v4_inaddr(&me, SQADDR_ASSERT_IS_V4);
	identChecklist.my_port = sqinet_get_port(&me);
	if (aclCheckFast(Config.accessList.identLookup, &identChecklist))
	    identStart4(&connState->me, &connState->peer, clientIdentDone, connState);
#endif
	if (s->http.tcp_keepalive.enabled) {
	    commSetTcpKeepalive(fd, s->http.tcp_keepalive.idle, s->http.tcp_keepalive.interval, s->http.tcp_keepalive.timeout);
	}
	if (Config.client_socksize > -1)
	    commSetTcpBufferSize(fd, Config.client_socksize);
	clientdbEstablished(sqinet_get_v4_inaddr(&peer, SQADDR_ASSERT_IS_V4), 1);
	incoming_sockets_accepted++;
	httpsAcceptSSL(connState, s->sslContext);
        sqinet_done(&peer);
        sqinet_done(&me);
    }
}

#endif /* USE_SSL */

#define SENDING_BODY 0
#define SENDING_HDRSONLY 1
static int
clientCheckTransferDone(clientHttpRequest * http)
{
    int sending = SENDING_BODY;
    StoreEntry *entry = http->entry;
    MemObject *mem;
    http_reply *reply;
    squid_off_t sendlen;
    if (entry == NULL)
	return 0;
    /*
     * For now, 'done_copying' is used for special cases like
     * Range and HEAD requests.
     */
    if (http->flags.done_copying)
	return 1;
    /*
     * Handle STORE_OK objects.
     * objectLen(entry) will be set proprely.
     */
    if (entry->store_status == STORE_OK) {
	if (http->out.offset >= objectLen(entry))
	    return 1;
	else
	    return 0;
    }
    /*
     * Now, handle STORE_PENDING objects
     */
    mem = entry->mem_obj;
    assert(mem != NULL);
    assert(http->request != NULL);
    reply = mem->reply;
    if (reply->hdr_sz == 0)
	return 0;		/* haven't found end of headers yet */
    else if (reply->sline.status == HTTP_OK)
	sending = SENDING_BODY;
    else if (reply->sline.status == HTTP_NO_CONTENT)
	sending = SENDING_HDRSONLY;
    else if (reply->sline.status == HTTP_NOT_MODIFIED)
	sending = SENDING_HDRSONLY;
    else if (reply->sline.status < HTTP_OK)
	sending = SENDING_HDRSONLY;
    else if (http->request->method->code == METHOD_HEAD)
	sending = SENDING_HDRSONLY;
    else
	sending = SENDING_BODY;
    /*
     * Figure out how much data we are supposed to send.
     * If we are sending a body and we don't have a content-length,
     * then we must wait for the object to become STORE_OK.
     */
    if (sending == SENDING_HDRSONLY)
	sendlen = reply->hdr_sz;
    else if (reply->content_length < 0)
	return 0;
    else
	sendlen = reply->content_length + reply->hdr_sz;
    /*
     * Now that we have the expected length, did we send it all?
     */
    if (http->out.offset < sendlen)
	return 0;
    else
	return 1;
}

static int
clientGotNotEnough(clientHttpRequest * http)
{
    squid_off_t cl = httpReplyBodySize(http->request->method, http->entry->mem_obj->reply);
    int hs = http->entry->mem_obj->reply->hdr_sz;
    if (cl < 0)
	return 0;
    if (http->out.offset != cl + hs)
	return 1;
    return 0;
}

/*
 * This function is designed to serve a fairly specific purpose.
 * Occasionally our vBNS-connected caches can talk to each other, but not
 * the rest of the world.  Here we try to detect frequent failures which
 * make the cache unusable (e.g. DNS lookup and connect() failures).  If
 * the failure:success ratio goes above 1.0 then we go into "hit only"
 * mode where we only return UDP_HIT or UDP_MISS_NOFETCH.  Neighbors
 * will only fetch HITs from us if they are using the ICP protocol.  We
 * stay in this mode for 5 minutes.
 * 
 * Duane W., Sept 16, 1996
 */

static void
checkFailureRatio(err_type etype, hier_code hcode)
{
    static double magic_factor = 100.0;
    double n_good;
    double n_bad;
    if (hcode == HIER_NONE)
	return;
    n_good = magic_factor / (1.0 + request_failure_ratio);
    n_bad = magic_factor - n_good;
    switch (etype) {
    case ERR_DNS_FAIL:
    case ERR_CONNECT_FAIL:
    case ERR_READ_ERROR:
	n_bad++;
	break;
    default:
	n_good++;
    }
    request_failure_ratio = n_bad / n_good;
    if (hit_only_mode_until > squid_curtime)
	return;
    if (request_failure_ratio < 1.0)
	return;
    debug(33, 0) ("Failure Ratio at %4.2f\n", request_failure_ratio);
    debug(33, 0) ("Going into hit-only-mode for %d minutes...\n",
	FAILURE_MODE_TIME / 60);
    hit_only_mode_until = squid_curtime + FAILURE_MODE_TIME;
    request_failure_ratio = 0.8;	/* reset to something less than 1.0 */
}

static void
clientHttpConnectionsOpen(void)
{
    http_port_list *s;
    int fd;
    int comm_flags;
    for (s = Config.Sockaddr.http; s; s = s->next) {
	comm_flags = COMM_NONBLOCKING;
	if (s->tproxy)
		comm_flags |= COMM_TPROXY_LCL;
	if (MAXHTTPPORTS == NHttpSockets) {
	    debug(1, 1) ("WARNING: You have too many 'http_port' lines.\n");
	    debug(1, 1) ("         The limit is %d\n", MAXHTTPPORTS);
	    continue;
	}
	if ((NHttpSockets == 0) && opt_stdin_overrides_http_port) {
	    fd = 0;
	    if (reconfiguring) {
		/* this one did not get closed, just reuse it */
		HttpSockets[NHttpSockets++] = fd;
		continue;
	    }
	    comm_fdopen(fd,
		SOCK_STREAM,
		no_addr,
		ntohs(s->s.sin_port),
		comm_flags,
		COMM_TOS_DEFAULT,
		"HTTP Socket");
	} else {
	    enter_suid();
	    fd = comm_open(SOCK_STREAM,
		IPPROTO_TCP,
		s->s.sin_addr,
		ntohs(s->s.sin_port),
		comm_flags,
		COMM_TOS_DEFAULT,
		"HTTP Socket");
	    leave_suid();
	}
	if (fd < 0)
	    continue;
	comm_listen(fd);
	commSetSelect(fd, COMM_SELECT_READ, httpAccept, s, 0);
	/*
	 * We need to set a defer handler here so that we don't
	 * peg the CPU with select() when we hit the FD limit.
	 */
	commSetDefer(fd, httpAcceptDefer, NULL);
	debug(1, 1) ("Accepting %s %sHTTP connections at %s, port %d, FD %d.\n",
	    s->transparent ? "transparently proxied" :
	    s->accel ? "accelerated" :
	    "proxy",
	    s->tproxy ? "and tproxy'ied " : "",
	    inet_ntoa(s->s.sin_addr),
	    (int) ntohs(s->s.sin_port),
	    fd);
	HttpSockets[NHttpSockets++] = fd;
    }
}

#if USE_SSL
static void
clientHttpsConnectionsOpen(void)
{
    https_port_list *s;
    int fd;
    for (s = Config.Sockaddr.https; s; s = (https_port_list *) s->http.next) {
	if (MAXHTTPPORTS == NHttpSockets) {
	    debug(1, 1) ("WARNING: You have too many 'https_port' lines.\n");
	    debug(1, 1) ("         The limit is %d\n", MAXHTTPPORTS);
	    continue;
	}
	if (!s->sslContext)
	    continue;
	enter_suid();
	fd = comm_open(SOCK_STREAM,
	    IPPROTO_TCP,
	    s->http.s.sin_addr,
	    ntohs(s->http.s.sin_port),
	    COMM_NONBLOCKING,
	    COMM_TOS_DEFAULT,
	    "HTTPS Socket");
	leave_suid();
	if (fd < 0)
	    continue;
	comm_listen(fd);
	commSetSelect(fd, COMM_SELECT_READ, httpsAccept, s, 0);
	commSetDefer(fd, httpAcceptDefer, NULL);
	debug(1, 1) ("Accepting HTTPS connections at %s, port %d, FD %d.\n",
	    inet_ntoa(s->http.s.sin_addr),
	    (int) ntohs(s->http.s.sin_port),
	    fd);
	HttpSockets[NHttpSockets++] = fd;
    }
}

#endif

void
clientOpenListenSockets(void)
{
    clientHttpConnectionsOpen();
#if USE_SSL
    clientHttpsConnectionsOpen();
#endif
    if (NHttpSockets < 1)
	fatal("Cannot open HTTP Port");
}
void
clientHttpConnectionsClose(void)
{
    int i = 0;
    if (opt_stdin_overrides_http_port && reconfiguring)
	i++;			/* skip closing & reopening first port because it is overridden */
    for (; i < NHttpSockets; i++) {
	if (HttpSockets[i] >= 0) {
	    debug(1, 1) ("FD %d Closing HTTP connection\n", HttpSockets[i]);
	    comm_close(HttpSockets[i]);
	    HttpSockets[i] = -1;
	}
    }
    NHttpSockets = 0;
}

static int
varyEvaluateMatch(StoreEntry * entry, request_t * request)
{
    const char *vary = request->vary_headers;
    int has_vary = httpHeaderHas(&entry->mem_obj->reply->header, HDR_VARY);
#if X_ACCELERATOR_VARY
    has_vary |= httpHeaderHas(&entry->mem_obj->reply->header, HDR_X_ACCELERATOR_VARY);
#endif
    if (!has_vary || !entry->mem_obj->vary_headers) {
	if (vary) {
	    /* Oops... something odd is going on here.. */
	    debug(33, 1) ("varyEvaluateMatch: Oops. Not a Vary object on second attempt, '%s' '%s'\n",
		entry->mem_obj->url, vary);
	    safe_free(request->vary_headers);
	    return VARY_CANCEL;
	}
	if (!has_vary) {
	    /* This is not a varying object */
	    return VARY_NONE;
	}
	/* virtual "vary" object found. Calculate the vary key and
	 * continue the search
	 */
	vary = httpMakeVaryMark(request, entry->mem_obj->reply);
	if (vary) {
	    return VARY_OTHER;
	} else {
	    /* Ouch.. we cannot handle this kind of variance */
	    /* XXX This cannot really happen, but just to be complete */
	    return VARY_CANCEL;
	}
    } else {
	if (!vary)
	    vary = httpMakeVaryMark(request, entry->mem_obj->reply);
	if (!vary) {
	    /* Ouch.. we cannot handle this kind of variance */
	    /* XXX This cannot really happen, but just to be complete */
	    return VARY_CANCEL;
	} else if (request->flags.collapsed) {
	    /* This request was merged before we knew the outcome. Don't trust the response */
	    /* restart vary processing from the beginning */
	    return VARY_RESTART;
	} else {
	    return VARY_MATCH;
	}
    }
}

/* This is a handler normally called by comm_close() */
static void
clientPinnedConnectionClosed(int fd, void *data)
{
    ConnStateData *conn = data;
    conn->pinning.fd = -1;
    if (conn->pinning.peer) {
	cbdataUnlock(conn->pinning.peer);
	conn->pinning.peer = NULL;
    }
    safe_free(conn->pinning.host);
    /* NOTE: pinning.pinned should be kept. This combined with fd == -1 at the end of a request indicates that the host
     * connection has gone away */
}

void
clientPinConnection(ConnStateData * conn, int fd, const request_t * request, peer * peer, int auth)
{
    fde *f;
    LOCAL_ARRAY(char, desc, FD_DESC_SZ);
    const char *host = request->host;
    const int port = request->port;
    if (!cbdataValid(conn))
	comm_close(fd);
    if (conn->pinning.fd == fd)
	return;
    else if (conn->pinning.fd != -1)
	comm_close(conn->pinning.fd);
    conn->pinning.fd = fd;
    safe_free(conn->pinning.host);
    conn->pinning.host = xstrdup(host);
    conn->pinning.port = port;
    conn->pinning.pinned = 1;
    if (conn->pinning.peer)
	cbdataUnlock(conn->pinning.peer);
    conn->pinning.peer = peer;
    if (peer)
	cbdataLock(conn->pinning.peer);
    conn->pinning.auth = auth;
    f = &fd_table[conn->fd];
    snprintf(desc, FD_DESC_SZ, "%s pinned connection for %s:%d (%d)",
	(auth || !peer) ? host : peer->name, f->ipaddrstr, (int) f->remote_port, conn->fd);
    fd_note(fd, desc);
    comm_add_close_handler(fd, clientPinnedConnectionClosed, conn);
}

int
clientGetPinnedInfo(const ConnStateData * conn, const request_t * request, peer ** peer)
{
    int fd = conn->pinning.fd;

    if (fd < 0)
	return -1;

    if (conn->pinning.auth && request && strcasecmp(conn->pinning.host, request->host) != 0) {
      err:
	comm_close(fd);
	return -1;
    }
    if (request && conn->pinning.port != request->port)
	goto err;
    if (conn->pinning.peer && !cbdataValid(conn->pinning.peer))
	goto err;
    *peer = conn->pinning.peer;
    return fd;
}

int
clientGetPinnedConnection(ConnStateData * conn, const request_t * request, const peer * peer, int *auth)
{
    int fd = conn->pinning.fd;

    if (fd < 0)
	return -1;

    if (conn->pinning.auth && request && strcasecmp(conn->pinning.host, request->host) != 0) {
      err:
	comm_close(fd);
	return -1;
    }
    *auth = conn->pinning.auth;
    if (peer != conn->pinning.peer)
	goto err;
    cbdataUnlock(conn->pinning.peer);
    conn->pinning.peer = NULL;
    conn->pinning.fd = -1;
    comm_remove_close_handler(fd, clientPinnedConnectionClosed, conn);
    return fd;
}

#if DELAY_POOLS
void
clientReassignDelaypools(void)
{
    dlink_node *i;
    for (i = ClientActiveRequests.head; i; i = i->next) {
	clientHttpRequest *http = i->data;
	assert(http);
	if (http->sc && http->log_type != LOG_TCP_DENIED && http->log_type != LOG_TAG_NONE)
	    delaySetStoreClient(http->sc, delayClient(http));
	if (http->reply)
	    http->delayMaxBodySize = 0;
	http->delayAssignedPool = 0;
	clientDelayMaxBodySize(http->request, http, http->reply);
    }
}
#endif
