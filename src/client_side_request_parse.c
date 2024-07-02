
/*
 * $Id: client_side.c 14461 2010-03-17 16:16:07Z adrian.chadd $
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

#include "client_side_conn.h"
#include "client_side_body.h"

#include "client_side_request.h"

#include "client_side_ranges.h"

#include "client_side.h"

static const char *const crlf = "\r\n";

#define FAILURE_MODE_TIME 300

/* Local functions */

static clientHttpRequest *parseHttpRequestAbort(ConnStateData * conn, method_t ** method_p, const char *uri);
static clientHttpRequest *parseHttpRequest(ConnStateData *, HttpMsgBuf *, method_t **, int *);

CBDATA_TYPE(clientHttpRequest);

/*
 * clientSetKeepaliveFlag() sets request->flags.proxy_keepalive.
 * This is the client-side persistent connection flag.  We need
 * to set this relatively early in the request processing
 * to handle hacks for broken servers and clients.
 */
static void
clientSetKeepaliveFlag(clientHttpRequest * http)
{
    request_t *request = http->request;
    const HttpHeader *req_hdr = &request->header;

    debug(33, 3) ("clientSetKeepaliveFlag: http_ver = %d.%d\n",
        request->http_ver.major, request->http_ver.minor);
    debug(33, 3) ("clientSetKeepaliveFlag: method = %s\n",
        urlMethodGetConstStr(request->method));
    {
        http_version_t http_ver;
        if (http->conn->port->http11)
            http_ver = request->http_ver;
        else
            httpBuildVersion(&http_ver, 1, 0);  /* we are HTTP/1.0, no matter what the client requests... */
        if (httpMsgIsPersistent(http_ver, req_hdr))
            request->flags.proxy_keepalive = 1;
    }
}

static int
clientCheckContentLength(request_t * r)
{
    switch (r->method->code) {
    case METHOD_GET:
    case METHOD_HEAD:
        /* We do not want to see a request entity on GET/HEAD requests */
        return (r->content_length <= 0 || Config.onoff.request_entities);
    default:
        /* For other types of requests we don't care */
        return 1;
    }
    /* NOT REACHED */
}

/*  
 * Calculates the maximum size allowed for an HTTP request body
 */ 
static void
clientMaxRequestBodySize(request_t * request, clientHttpRequest * http)
{
    body_size *bs;
    aclCheck_t *checklist;
    if (http->log_type == LOG_TCP_DENIED)
        return;
    bs = (body_size *) Config.RequestBodySize.head;
    http->maxRequestBodySize = 0;
    while (bs) {
        checklist = clientAclChecklistCreate(bs->access_list, http);
        if (aclCheckFast(bs->access_list, checklist) != 1) {
            /* deny - skip this entry */
            bs = (body_size *) bs->node.next;
        } else {
            /* Allow - use this entry */
            http->maxRequestBodySize = bs->maxsize;
            bs = NULL;
            debug(58, 3) ("clientMaxRequestBodySize: Setting maxRequestBodySize to %ld\n", (long int) http->maxRequestBodySize);
        }
        aclChecklistFree(checklist);
    }
}

static int
clientRequestBodyTooLarge(clientHttpRequest * http, request_t * request)
{
 
    if (http->maxRequestBodySize == -1) {
        clientMaxRequestBodySize(request, http);
    }
    if (0 == http->maxRequestBodySize)
        return 0;               /* disabled */
    if (request->content_length < 0)
        return 0;               /* unknown, bug? */
    if (request->content_length > http->maxRequestBodySize)
        return 1;               /* too large */
    return 0;
}

/*
 * Abort the current request during parsing.
 *
 * If the method pointer is set, leave it. Otherwise, set it to
 * METHOD_NONE so it's set to -something-.
 */
static clientHttpRequest *
parseHttpRequestAbort(ConnStateData * conn, method_t ** method_p, const char *uri)
{
    clientHttpRequest *http;
    CBDATA_INIT_TYPE(clientHttpRequest);
    http = cbdataAlloc(clientHttpRequest);
    http->conn = conn;
    http->start = current_time;
    http->req_sz = conn->in.offset;
    http->uri = xstrdup(uri);
    http->log_uri = xstrdup(uri);
    http->range_iter.boundary = StringNull;
    httpBuildVersion(&http->http_ver, 1, 0);
    dlinkAdd(http, &http->active, &ClientActiveRequests);
    if (method_p && !*method_p)
        *method_p = urlMethodGetKnownByCode(METHOD_NONE);
    return http;
}

static int
parseHttpConnectRequest(ConnStateData *conn, clientHttpRequest *http)
{
	if (http->http_ver.major < 1) {
	    debug(33, 1) ("parseHttpRequest: Invalid HTTP version\n");
	    return 0;
	}
	if (conn->port->accel) {
	    debug(33, 1) ("parseHttpRequest: CONNECT not valid in accelerator mode\n");
	    return 0;
	}
	return 1;
}

static int
parseHttpInternalRequest(ConnStateData *conn, clientHttpRequest *http, const char *url)
{
	http->uri = xstrdup(internalStoreUri("", url));
	http->flags.internal = 1;
	http->flags.accel = 1;
	debug(33, 5) ("INTERNAL REWRITE: '%s'\n", http->uri);
	return 1;
}
static int
parseHttpTransparentRequest(ConnStateData *conn, clientHttpRequest *http, const char *url, const char *req_hdr)
{
	int port = 0;
	const char *host = mime_get_header(req_hdr, "Host");
	char *portstr;

	if (host && (portstr = strchr(host, ':')) != NULL) {
	    *portstr++ = '\0';
	    port = atoi(portstr);
	}
	http->flags.transparent = 1;

	if (Config.onoff.accel_no_pmtu_disc)
	    commSetNoPmtuDiscover(conn->fd);

	if (conn->port->transparent && clientNatLookup(conn) == 0)
	    conn->transparent = 1;
	if (!host && conn->transparent) {
	    port = ntohs(conn->me.sin_port);
	    if (!host)
		host = inet_ntoa(conn->me.sin_addr);
	}
	if (host) {
	    size_t url_sz = 10 + strlen(host) + 6 + strlen(url) + 32 + Config.appendDomainLen;
	    http->uri = xcalloc(url_sz, 1);
	    if (port) {
		snprintf(http->uri, url_sz, "%s://%s:%d%s",
		    conn->port->protocol, host, port, url);
	    } else {
		snprintf(http->uri, url_sz, "%s://%s%s",
		    conn->port->protocol, host, url);
	    }
	} else if (internalCheck(url)) {
	    parseHttpInternalRequest(conn, http, url);
	} else {
	    return 0;
	}
	return 1;
}

static int
parseHttpAccelRequest(ConnStateData *conn, clientHttpRequest *http, const char *url, const char *req_hdr)
{
	int vhost = conn->port->vhost;
	int vport = conn->port->vport;
	const char *t = NULL;

	http->flags.accel = 1;
	if (*url != '/' && !vhost && strncasecmp(url, "cache_object://", 15) != 0) {
	    url = strstr(url, "//");
	    if (!url)
		return 0;
	    url = strchr(url + 2, '/');
	    if (!url)
		url = (char *) "/";
	}
	if (*url != '/') {
	    /* Fully qualified URL. Nothing special to do */
	} else if (conn->port->accel) {
	    const char *host = NULL;
	    int port;
	    size_t url_sz;
	    if (vport > 0)
		port = vport;
	    else
		port = htons(http->conn->me.sin_port);
	    if (vhost && (t = mime_get_header(req_hdr, "Host")))
		host = t;
	    else if (conn->port->defaultsite)
		host = conn->port->defaultsite;
	    else if (vport == -1)
		host = inet_ntoa(http->conn->me.sin_addr);
	    else
		host = getMyHostname();
	    url_sz = strlen(url) + 32 + Config.appendDomainLen + strlen(host);
	    http->uri = xcalloc(url_sz, 1);
	    if (strchr(host, ':'))
		snprintf(http->uri, url_sz, "%s://%s%s",
		    conn->port->protocol, host, url);
	    else
		snprintf(http->uri, url_sz, "%s://%s:%d%s",
		    conn->port->protocol, host, port, url);
	    debug(33, 5) ("VHOST REWRITE: '%s'\n", http->uri);
	} else if (internalCheck(url)) {
	    parseHttpInternalRequest(conn, http, url);
	} else {
	    return 0;
	}
    return 1;
}

/*
 *  parseHttpRequest()
 * 
 *  Returns
 *   NULL on error or incomplete request
 *    a clientHttpRequest structure on success
 *    method_p may be set to a parsed method, or METHOD_NONE, or NULL.
 *    It's the callers' responsibility to transfer or free the method.
 */
static clientHttpRequest *
parseHttpRequest(ConnStateData * conn, HttpMsgBuf * hmsg, method_t ** method_p, int *status)
{
    LOCAL_ARRAY(char, urlbuf, MAX_URL);
    char *url = urlbuf;
    const char *req_hdr = NULL;
    http_version_t http_ver;
    size_t header_sz;		/* size of headers, not including first line */
    size_t prefix_sz;		/* size of whole request (req-line + headers) */
    size_t req_sz;
    method_t *method;
    clientHttpRequest *http = NULL;
#if THIS_VIOLATES_HTTP_SPECS_ON_URL_TRANSFORMATION
    char *t;
#endif
    int ret;

    /* pre-set these values to make aborting simpler */
    *method_p = urlMethodGetKnownByCode(METHOD_NONE);
    *status = -1;

    /* Parse the request line */
    ret = httpMsgParseRequestLine(hmsg);
    if (ret == -1)
	return parseHttpRequestAbort(conn, method_p, "error:invalid-request");
    if (ret == 0) {
	debug(33, 5) ("Incomplete request, waiting for end of request line\n");
	*status = 0;
	return NULL;
    }
    /* If HTTP/0.9 then there's no headers */
    if (hmsg->v_maj == 0 && hmsg->v_min == 9) {
	req_sz = hmsg->r_len;
    } else {
	req_sz = httpMsgFindHeadersEnd(hmsg);
	if (req_sz == 0) {
	    debug(33, 5) ("Incomplete request, waiting for end of headers\n");
	    *status = 0;
	    return NULL;
	}
    }
    /* Set version */
    httpBuildVersion(&http_ver, hmsg->v_maj, hmsg->v_min);

    /* Enforce max_request_size */
    if (req_sz >= Config.maxRequestHeaderSize) {
	debug(33, 5) ("parseHttpRequest: Too large request\n");
	return parseHttpRequestAbort(conn, method_p, "error:request-too-large");
    }
    /* Wrap the request method */
    method = urlMethodGet(hmsg->buf + hmsg->m_start, hmsg->m_len);

    debug(33, 5) ("parseHttpRequest: Method is '%s'\n", urlMethodGetConstStr(method));
    if (method->code == METHOD_OTHER) {
	debug(33, 5) ("parseHttpRequest: Unknown method, continuing regardless");
    }
    *method_p = method;

    /* Make sure URL fits inside MAX_URL */
    if (hmsg->u_len >= MAX_URL) {
	debug(33, 1) ("parseHttpRequest: URL too big (%d) chars: %s\n", hmsg->u_len, hmsg->buf + hmsg->u_start);
	return parseHttpRequestAbort(conn, method_p, "error:request-too-large");
    }
    xmemcpy(urlbuf, hmsg->buf + hmsg->u_start, hmsg->u_len);
    /* XXX off-by-one termination error? */
    urlbuf[hmsg->u_len] = '\0';
    debug(33, 5) ("parseHttpRequest: URI is '%s'\n", urlbuf);

    /*
     * Process headers after request line
     * XXX at this point we really should just parse the damned headers rather than doing
     * it later, allowing us to then do the URL acceleration stuff withuot too much hackery.
     */
    /* XXX re-evaluate all of these values and use whats in hmsg instead! */
    req_hdr = hmsg->buf + hmsg->r_len;
    header_sz = hmsg->h_len;
    debug(33, 3) ("parseHttpRequest: req_hdr = {%s}\n", req_hdr);

    prefix_sz = req_sz;
    debug(33, 3) ("parseHttpRequest: prefix_sz = %d, req_line_sz = %d\n",
	(int) prefix_sz, (int) hmsg->r_len);
    assert(prefix_sz <= conn->in.offset);

    /* Ok, all headers are received */
    CBDATA_INIT_TYPE(clientHttpRequest);
    http = cbdataAlloc(clientHttpRequest);
    http->http_ver = http_ver;
    http->conn = conn;
    http->start = current_time;
    http->req_sz = prefix_sz;
    http->range_iter.boundary = StringNull;
    http->maxRequestBodySize = -1;
    dlinkAdd(http, &http->active, &ClientActiveRequests);

    debug(33, 5) ("parseHttpRequest: Request Header is\n%s\n", hmsg->buf + hmsg->req_end);

#if THIS_VIOLATES_HTTP_SPECS_ON_URL_TRANSFORMATION
    if ((t = strchr(url, '#')))	/* remove HTML anchors */
	*t = '\0';
#endif

    /* handle "accelerated" objects (and internal) */
    if (method->code == METHOD_CONNECT) {
        if (! parseHttpConnectRequest(conn, http))
		goto invalid_request;
    } else if (*url == '/' && Config.onoff.global_internal_static && internalCheck(url)) {
        (void) parseHttpInternalRequest(conn, http, url);
    } else if (*url == '/' && conn->port->transparent) {
        if (! parseHttpTransparentRequest(conn, http, url, req_hdr))
            goto invalid_request;
    } else if (*url == '/' || conn->port->accel) {
	if (! parseHttpAccelRequest(conn, http, url, req_hdr))
            goto invalid_request;
    }
    if (!http->uri) {
	/* No special rewrites have been applied above, use the
	 * requested url. may be rewritten later, so make extra room */
	size_t url_sz = strlen(url) + Config.appendDomainLen + 5;
	http->uri = xcalloc(url_sz, 1);
	strcpy(http->uri, url);
    }
    debug(33, 5) ("parseHttpRequest: Complete request received\n");
    *status = 1;
    return http;

  invalid_request:
    /* This tries to back out what is done above */
    dlinkDelete(&http->active, &ClientActiveRequests);
    safe_free(http->uri);
    cbdataFree(http);
    return parseHttpRequestAbort(conn, method_p, "error:invalid-request");
}

/*
 * Attempt to parse a request in the conn buffer
 *
 * Return the number of bytes to consume from the buffer.
 * >0 : consume X bytes and try parsing next request
 * =0 : couldn't consume anything this trip (partial request); stop parsing & read more data
 * <0 : error; stop parsing
 */
int
clientTryParseRequest(ConnStateData * conn)
{
    int fd = conn->fd;
    int nrequests;
    dlink_node *n;
    clientHttpRequest *http = NULL;
    method_t *method = NULL;
    ErrorState *err = NULL;
    int parser_return_code = 0;
    request_t *request = NULL;
    HttpMsgBuf msg;
    int ret = -1;

    /* Skip leading (and trailing) whitespace */
    while (conn->in.offset > 0 && xisspace(conn->in.buf[0])) {
	xmemmove(conn->in.buf, conn->in.buf + 1, conn->in.offset - 1);
	conn->in.offset--;
    }
    conn->in.buf[conn->in.offset] = '\0';	/* Terminate the string */
    if (conn->in.offset == 0) {
	ret = 0;
	goto finish;
    }

    HttpMsgBufInit(&msg, conn->in.buf, conn->in.offset);	/* XXX for now there's no deallocation function needed but this may change */
    /* Limit the number of concurrent requests to 2 */
    for (n = conn->reqs.head, nrequests = 0; n; n = n->next, nrequests++);
    if (nrequests >= (Config.onoff.pipeline_prefetch ? 2 : 1)) {
	debug(33, 3) ("clientTryParseRequest: FD %d max concurrent requests reached\n", fd);
	debug(33, 5) ("clientTryParseRequest: FD %d defering new request until one is done\n", fd);
	conn->defer.until = squid_curtime + 100;	/* Reset when a request is complete */
	ret = 0;
	goto finish;
    }
    conn->in.buf[conn->in.offset] = '\0';	/* Terminate the string */
    if (nrequests == 0)
	fd_note_static(conn->fd, "Reading next request");
    /* Process request */
    http = parseHttpRequest(conn, &msg, &method, &parser_return_code);
    if (!http) {
	/* falls through here to the "if parser_return_code == 0"; not sure what will
	 * happen if http == NULL and parser_return_code != 0 .. */
    }
    if (http) {
	/* add to the client request queue */
	dlinkAddTail(http, &http->node, &conn->reqs);
	conn->nrequests++;
	commSetTimeout(fd, Config.Timeout.lifetime, clientLifetimeTimeout, http);
	if (parser_return_code < 0) {
	    debug(33, 1) ("clientTryParseRequest: FD %d (%s:%d) Invalid Request\n", fd, fd_table[fd].ipaddrstr, fd_table[fd].remote_port);
	    err = errorCon(ERR_INVALID_REQ, HTTP_BAD_REQUEST, NULL);
	    err->src_addr = conn->peer.sin_addr;
	    err->request_hdrs = xstrdup(conn->in.buf);
	    http->log_type = LOG_TCP_DENIED;
	    http->entry = clientCreateStoreEntry(http, method, null_request_flags);
	    errorAppendEntry(http->entry, err);
	    ret = -1;
	    goto finish;
	}
	if ((request = urlParse(method, http->uri)) == NULL) {
	    debug(33, 5) ("Invalid URL: %s\n", http->uri);
	    err = errorCon(ERR_INVALID_URL, HTTP_BAD_REQUEST, NULL);
	    err->src_addr = conn->peer.sin_addr;
	    err->url = xstrdup(http->uri);
	    http->al.http.code = err->http_status;
	    http->log_type = LOG_TCP_DENIED;
	    http->entry = clientCreateStoreEntry(http, method, null_request_flags);
	    errorAppendEntry(http->entry, err);
	    ret = -1;
	    goto finish;
	}
	/* compile headers */
	/* we should skip request line! */
	if ((http->http_ver.major >= 1) && !httpMsgParseRequestHeader(request, &msg)) {
	    debug(33, 1) ("Failed to parse request headers: %s\n%s\n",
		http->uri, msg.buf + msg.req_end);
	    err = errorCon(ERR_INVALID_URL, HTTP_BAD_REQUEST, request);
	    err->url = xstrdup(http->uri);
	    http->al.http.code = err->http_status;
	    http->log_type = LOG_TCP_DENIED;
	    http->entry = clientCreateStoreEntry(http, method, null_request_flags);
	    errorAppendEntry(http->entry, err);
	    ret = -1;
	    goto finish;
	}
	/*
	 * If we read past the end of this request, move the remaining
	 * data to the beginning
	 */
	assert(conn->in.offset >= http->req_sz);
	conn->in.offset -= http->req_sz;
	debug(33, 5) ("removing %d bytes; conn->in.offset = %d\n", (int) http->req_sz, (int) conn->in.offset);
	if (conn->in.offset > 0)
	    xmemmove(conn->in.buf, conn->in.buf + http->req_sz, conn->in.offset);

	if (!http->flags.internal && internalCheck(strBuf(request->urlpath))) {
	    if (internalHostnameIs(request->host))
		http->flags.internal = 1;
	    else if (Config.onoff.global_internal_static && internalStaticCheck(strBuf(request->urlpath)))
		http->flags.internal = 1;
	    if (http->flags.internal) {
		request_t *old_request = requestLink(request);
		request = urlParse(method, internalStoreUri("", strBuf(request->urlpath)));
		httpHeaderAppend(&request->header, &old_request->header);
		requestUnlink(old_request);
	    }
	}
	if (conn->port->urlgroup)
	    request->urlgroup = xstrdup(conn->port->urlgroup);
	request->flags.tproxy = conn->port->tproxy && need_linux_tproxy;
	request->flags.accelerated = http->flags.accel;
	request->flags.no_direct = request->flags.accelerated ? !conn->port->allow_direct : 0;
	request->flags.transparent = http->flags.transparent;
	/*
	 * cache the Content-length value in request_t.
	 */
	request->content_length = httpHeaderGetSize(&request->header,
	    HDR_CONTENT_LENGTH);
	request->flags.internal = http->flags.internal;
	request->client_addr = conn->peer.sin_addr;
	request->client_port = ntohs(conn->peer.sin_port);
#if FOLLOW_X_FORWARDED_FOR
	request->indirect_client_addr = request->client_addr;
#endif /* FOLLOW_X_FORWARDED_FOR */
	request->my_addr = conn->me.sin_addr;
	request->my_port = ntohs(conn->me.sin_port);
	request->http_ver = http->http_ver;
	if (!urlCheckRequest(request)) {
	    err = errorCon(ERR_UNSUP_REQ, HTTP_NOT_IMPLEMENTED, request);
	    request->flags.proxy_keepalive = 0;
	    http->al.http.code = err->http_status;
	    http->log_type = LOG_TCP_DENIED;
	    http->entry = clientCreateStoreEntry(http, request->method, null_request_flags);
	    errorAppendEntry(http->entry, err);
	    ret = -1;
	    goto finish;
	}
	if (!clientCheckContentLength(request) || httpHeaderHas(&request->header, HDR_TRANSFER_ENCODING)) {
	    err = errorCon(ERR_INVALID_REQ, HTTP_LENGTH_REQUIRED, request);
	    http->al.http.code = err->http_status;
	    http->log_type = LOG_TCP_DENIED;
	    http->entry = clientCreateStoreEntry(http, request->method, null_request_flags);
	    errorAppendEntry(http->entry, err);
	    ret = -1;
	    goto finish;
	}
	http->request = requestLink(request);
	http->orig_request = requestLink(request);
	clientSetKeepaliveFlag(http);
	/* Do we expect a request-body? */
	if (request->content_length > 0) {
	    conn->body.size_left = request->content_length;
	    debug(33, 2) ("clientTryParseRequest: %p: FD %d: request body is %d bytes in size\n", request, conn->fd, (int) conn->body.size_left);
	    request->body_reader = clientReadBody;
	    request->body_reader_data = conn;
	    cbdataLock(conn);
	    /* Is it too large? */
	    if (clientRequestBodyTooLarge(http, request)) {
		err = errorCon(ERR_TOO_BIG, HTTP_REQUEST_ENTITY_TOO_LARGE, request);
		http->log_type = LOG_TCP_DENIED;
		http->entry = clientCreateStoreEntry(http, urlMethodGetKnownByCode(METHOD_NONE), null_request_flags);
		errorAppendEntry(http->entry, err);
		ret = -1;
		goto finish;
	    }
	}
	if (request->method->code == METHOD_CONNECT) {
	    /* Stop reading requests... */
	    commSetSelect(fd, COMM_SELECT_READ, NULL, NULL, 0);
	    if (!DLINK_ISEMPTY(conn->reqs) && DLINK_HEAD(conn->reqs) == http)
		clientCheckFollowXForwardedFor(http);
	    else {
		debug(33, 1) ("WARNING: pipelined CONNECT request seen from %s\n", inet_ntoa(http->conn->peer.sin_addr));
		debugObj(33, 1, "Previous request:\n", ((clientHttpRequest *) DLINK_HEAD(conn->reqs))->request,
		    (ObjPackMethod) & httpRequestPackDebug);
		debugObj(33, 1, "This request:\n", request, (ObjPackMethod) & httpRequestPackDebug);
	    }
	    ret = -2;
	    goto finish;
	} else {
	    clientCheckFollowXForwardedFor(http);
	}
    } else if (parser_return_code == 0) {
	/*
	 *    Partial request received; reschedule until parseHttpRequest()
	 *    is happy with the input
	 */
	if (conn->in.offset >= Config.maxRequestHeaderSize) {
	    /* The request is too large to handle */
	    debug(33, 1) ("Request header is too large (%d bytes)\n",
		(int) conn->in.offset);
	    debug(33, 1) ("Config 'request_header_max_size'= %ld bytes.\n",
		(long int) Config.maxRequestHeaderSize);
	    err = errorCon(ERR_TOO_BIG, HTTP_REQUEST_URI_TOO_LONG, NULL);
	    err->src_addr = conn->peer.sin_addr;
	    http = parseHttpRequestAbort(conn, &method, "error:request-too-large");
	    /* add to the client request queue */
	    dlinkAddTail(http, &http->node, &conn->reqs);
	    http->log_type = LOG_TCP_DENIED;
	    http->entry = clientCreateStoreEntry(http, method, null_request_flags);
	    errorAppendEntry(http->entry, err);
	    ret = -1;
	    goto finish;
	}
	ret = 0;
	goto finish;
    }
    if (!cbdataValid(conn)) {
    	ret = -1;
	goto finish;
    }

    /* 
     * For now we assume "here" means "we parsed a valid request. This might not be the case
     * as I might've broken up clientReadRequest() wrong. Quite a bit more work should be
     * done to simplify this code anyway so the first step is identifying the cases where
     * this isn't true.
     */
    assert(http != NULL);
    assert(http->req_sz > 0);
    ret = http->req_sz;

finish:
    if (method)
    	urlMethodFree(method);
    return ret;
}

