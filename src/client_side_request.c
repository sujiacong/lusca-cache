#include "squid.h"

/* Client-side request related pipeline routines */

#if FOLLOW_X_FORWARDED_FOR
static void clientFollowXForwardedForStart(void *data);
static void clientFollowXForwardedForNext(void *data);
static void clientFollowXForwardedForDone(int answer, void *data);   
#endif /* FOLLOW_X_FORWARDED_FOR */

/* no-cache */

static void
clientCheckNoCacheDone(int answer, void *data)
{   
    clientHttpRequest *http = data;
    http->request->flags.cachable = answer;
    http->acl_checklist = NULL;
    clientProcessRequest(http);
}

static void
clientCheckNoCache(clientHttpRequest * http)
{   
    if (Config.accessList.noCache && http->request->flags.cachable) {
        http->acl_checklist = clientAclChecklistCreate(Config.accessList.noCache, http);
        aclNBCheck(http->acl_checklist, clientCheckNoCacheDone, http);
    } else {
        clientCheckNoCacheDone(http->request->flags.cachable, http);
    }
}

/* http_access2 */

static void
clientAccessCheckDone2(int answer, void *data)
{
    clientHttpRequest *http = data;
    err_type page_id;
    http_status status;
    ErrorState *err = NULL;
    char *proxy_auth_msg = NULL;
    debug(33, 2) ("The request %s %s is %s, because it matched '%s'\n",
	urlMethodGetConstStr(http->request->method), http->uri,
	answer == ACCESS_ALLOWED ? "ALLOWED" : "DENIED",
	AclMatchedName ? AclMatchedName : "NO ACL's");
    proxy_auth_msg = authenticateAuthUserRequestMessage(http->conn->auth_user_request ? http->conn->auth_user_request : http->request->auth_user_request);
    http->acl_checklist = NULL;
    if (answer == ACCESS_ALLOWED) {
	clientCheckNoCache(http);
    } else {
	int require_auth = (answer == ACCESS_REQ_PROXY_AUTH || aclIsProxyAuth(AclMatchedName));
	debug(33, 5) ("Access Denied: %s\n", http->uri);
	debug(33, 5) ("AclMatchedName = %s\n",
	    AclMatchedName ? AclMatchedName : "<null>");
	if (require_auth)
	    debug(33, 5) ("Proxy Auth Message = %s\n",
		proxy_auth_msg ? proxy_auth_msg : "<null>");
	/*
	 * NOTE: get page_id here, based on AclMatchedName because
	 * if USE_DELAY_POOLS is enabled, then AclMatchedName gets
	 * clobbered in the clientCreateStoreEntry() call
	 * just below.  Pedro Ribeiro <pribeiro@isel.pt>
	 */
	page_id = aclGetDenyInfoPage(&Config.denyInfoList, AclMatchedName, answer != ACCESS_REQ_PROXY_AUTH);
	http->log_type = LOG_TCP_DENIED;
	http->entry = clientCreateStoreEntry(http, http->request->method,
	    null_request_flags);
	if (require_auth) {
	    if (!http->flags.accel) {
		/* Proxy authorisation needed */
		status = HTTP_PROXY_AUTHENTICATION_REQUIRED;
	    } else {
		/* WWW authorisation needed */
		status = HTTP_UNAUTHORIZED;
	    }
	    if (page_id == ERR_NONE)
		page_id = ERR_CACHE_ACCESS_DENIED;
	} else {
	    status = HTTP_FORBIDDEN;
	    if (page_id == ERR_NONE)
		page_id = ERR_ACCESS_DENIED;
	}
	err = errorCon(page_id, status, http->orig_request);
	if (http->conn->auth_user_request)
	    err->auth_user_request = http->conn->auth_user_request;
	else if (http->request->auth_user_request)
	    err->auth_user_request = http->request->auth_user_request;
	/* lock for the error state */
	if (err->auth_user_request)
	    authenticateAuthUserRequestLock(err->auth_user_request);
	err->callback_data = NULL;
	errorAppendEntry(http->entry, err);
    }
}

void
clientAccessCheck2(void *data)
{
    clientHttpRequest *http = data;
    if (Config.accessList.http2 && !http->redirect.status) {
        http->acl_checklist = clientAclChecklistCreate(Config.accessList.http2, http);
        aclNBCheck(http->acl_checklist, clientAccessCheckDone2, http);
    } else {
        clientCheckNoCache(http);
    }
}

/* Completion of URL / store URL rewriters */

/*
 * This is called by the last client request rewriter chain thing.
 */
void
clientFinishRewriteStuff(clientHttpRequest * http)
{
    /* This is the final part of the rewrite chain - this should be broken out! */
    clientInterpretRequestHeaders(http);
    /* XXX This really should become a ref-counted string type pointer, not a copy! */
    fd_note(http->conn->fd, http->uri);
#if HEADERS_LOG
    headersLog(0, 1, http->request->method, http->request);
#endif
    clientAccessCheck2(http);
 
}

/* http_access check */

static void
clientAccessCheckDone(int answer, void *data)
{
    clientHttpRequest *http = data;
    err_type page_id;
    http_status status;
    ErrorState *err = NULL;
    char *proxy_auth_msg = NULL;
    debug(33, 2) ("The request %s %s is %s, because it matched '%s'\n",
	urlMethodGetConstStr(http->request->method), http->uri,
	answer == ACCESS_ALLOWED ? "ALLOWED" : "DENIED",
	AclMatchedName ? AclMatchedName : "NO ACL's");
    proxy_auth_msg = authenticateAuthUserRequestMessage(http->conn->auth_user_request ? http->conn->auth_user_request : http->request->auth_user_request);
    http->acl_checklist = NULL;
    if (answer == ACCESS_ALLOWED) {
	safe_free(http->uri);
	http->uri = xstrdup(urlCanonical(http->request));
	assert(http->redirect_state == REDIRECT_NONE);
	http->redirect_state = REDIRECT_PENDING;
	clientRedirectStart(http);
    } else {
	int require_auth = (answer == ACCESS_REQ_PROXY_AUTH || aclIsProxyAuth(AclMatchedName)) && !http->request->flags.transparent;
	debug(33, 5) ("Access Denied: %s\n", http->uri);
	debug(33, 5) ("AclMatchedName = %s\n",
	    AclMatchedName ? AclMatchedName : "<null>");
	debug(33, 5) ("Proxy Auth Message = %s\n",
	    proxy_auth_msg ? proxy_auth_msg : "<null>");
	/*
	 * NOTE: get page_id here, based on AclMatchedName because
	 * if USE_DELAY_POOLS is enabled, then AclMatchedName gets
	 * clobbered in the clientCreateStoreEntry() call
	 * just below.  Pedro Ribeiro <pribeiro@isel.pt>
	 */
	page_id = aclGetDenyInfoPage(&Config.denyInfoList, AclMatchedName, answer != ACCESS_REQ_PROXY_AUTH);
	http->log_type = LOG_TCP_DENIED;
	http->entry = clientCreateStoreEntry(http, http->request->method,
	    null_request_flags);
	if (require_auth) {
	    if (!http->flags.accel) {
		/* Proxy authorisation needed */
		status = HTTP_PROXY_AUTHENTICATION_REQUIRED;
	    } else {
		/* WWW authorisation needed */
		status = HTTP_UNAUTHORIZED;
	    }
	    if (page_id == ERR_NONE)
		page_id = ERR_CACHE_ACCESS_DENIED;
	} else {
	    status = HTTP_FORBIDDEN;
	    if (page_id == ERR_NONE)
		page_id = ERR_ACCESS_DENIED;
	}
	err = errorCon(page_id, status, http->orig_request);
	if (http->conn->auth_user_request)
	    err->auth_user_request = http->conn->auth_user_request;
	else if (http->request->auth_user_request)
	    err->auth_user_request = http->request->auth_user_request;
	/* lock for the error state */
	if (err->auth_user_request)
	    authenticateAuthUserRequestLock(err->auth_user_request);
	err->callback_data = NULL;
	errorAppendEntry(http->entry, err);
    }
}

static void
clientAccessCheck(void *data)
{
    clientHttpRequest *http = data;
    http->acl_checklist = clientAclChecklistCreate(Config.accessList.http, http);
    aclNBCheck(http->acl_checklist, clientAccessCheckDone, http);
}

/* X-Forwarded-For processing */

#if FOLLOW_X_FORWARDED_FOR
/*
 * clientFollowXForwardedForStart() copies the X-Forwarded-For
 * header into x_forwarded_for_iterator and passes control to
 * clientFollowXForwardedForNext().
 *
 * clientFollowXForwardedForNext() checks the indirect_client_addr
 * against the followXFF ACL and passes the result to
 * clientFollowXForwardedForDone().
 *
 * clientFollowXForwardedForDone() either grabs the next address
 * from the tail of x_forwarded_for_iterator and loops back to
 * clientFollowXForwardedForNext(), or cleans up and passes control to
 * clientAccessCheck().
 */

static void
clientFollowXForwardedForStart(void *data)
{
    clientHttpRequest *http = data;
    request_t *request = http->request;
    request->x_forwarded_for_iterator = httpHeaderGetList(
	&request->header, HDR_X_FORWARDED_FOR);
    debug(33, 5) ("clientFollowXForwardedForStart: indirect_client_addr=%s XFF='%.*s'\n",
	inet_ntoa(request->indirect_client_addr),
	strLen2(request->x_forwarded_for_iterator),
	strBuf2(request->x_forwarded_for_iterator));
    clientFollowXForwardedForNext(http);
}

static void
clientFollowXForwardedForNext(void *data)
{
    clientHttpRequest *http = data;
    request_t *request = http->request;
    debug(33, 5) ("clientFollowXForwardedForNext: indirect_client_addr=%s XFF='%.*s'\n",
	inet_ntoa(request->indirect_client_addr),
	strLen2(request->x_forwarded_for_iterator),
	strBuf2(request->x_forwarded_for_iterator));
    if (strLen(request->x_forwarded_for_iterator) != 0) {
	/* check the acl to see whether to believe the X-Forwarded-For header */
	http->acl_checklist = clientAclChecklistCreate(
	    Config.accessList.followXFF, http);
	aclNBCheck(http->acl_checklist, clientFollowXForwardedForDone, http);
    } else {
	/* nothing left to follow */
	debug(33, 5) ("clientFollowXForwardedForNext: nothing more to do\n");
	clientFollowXForwardedForDone(-1, http);
    }
}

static void
clientFollowXForwardedForDone(int answer, void *data)
{
    clientHttpRequest *http = data;
    request_t *request = http->request;
    /*
     * answer should be be ACCESS_ALLOWED or ACCESS_DENIED if we are
     * called as a result of ACL checks, or -1 if we are called when
     * there's nothing left to do.
     */
    if (answer == ACCESS_ALLOWED) {
	/*
	 * The IP address currently in request->indirect_client_addr
	 * is trusted to use X-Forwarded-For.  Remove the last
	 * comma-delimited element from x_forwarded_for_iterator and use
	 * it to to replace indirect_client_addr, then repeat the cycle.
	 */
	const char *p;
	char *asciiaddr;
	int l;
	struct in_addr addr;
	debug(33, 5) ("clientFollowXForwardedForDone: indirect_client_addr=%s is trusted\n",
	    inet_ntoa(request->indirect_client_addr));
	p = strBuf2(request->x_forwarded_for_iterator);
	l = strLen2(request->x_forwarded_for_iterator);

	/*
	 * XXX x_forwarded_for_iterator should really be a list of
	 * IP addresses, but it's a String instead.  We have to
	 * walk backwards through the String, biting off the last
	 * comma-delimited part each time.  As long as the data is in
	 * a String, we should probably implement and use a variant of
	 * strListGetItem() that walks backwards instead of forwards
	 * through a comma-separated list.  But we don't even do that;
	 * we just do the work in-line here.
	 */
	/* skip trailing space and commas */
	while (l > 0 && (p[l - 1] == ',' || xisspace(p[l - 1])))
	    l--;
	strCut(&request->x_forwarded_for_iterator, l);
	/* look for start of last item in list */
	while (l > 0 && !(p[l - 1] == ',' || xisspace(p[l - 1])))
	    l--;

	/* Take a temporary copy of the buffer so inet_aton() can run on it */
        asciiaddr = stringDupToCOffset(&request->x_forwarded_for_iterator, l);
	if (inet_aton(asciiaddr, &addr) == 0) {
	    /* the address is not well formed; do not use it */
	    debug(33, 3) ("clientFollowXForwardedForDone: malformed address '%s'\n",
		asciiaddr);
	    safe_free(asciiaddr);
	    goto done;
	}
	debug(33, 3) ("clientFollowXForwardedForDone: changing indirect_client_addr from %s to '%s'\n",
	    inet_ntoa(request->indirect_client_addr),
	    asciiaddr);
	safe_free(asciiaddr);
	request->indirect_client_addr = addr;
	strCut(&request->x_forwarded_for_iterator, l);
	if (!Config.onoff.acl_uses_indirect_client) {
	    /*
	     * If acl_uses_indirect_client is off, then it's impossible
	     * to follow more than one level of X-Forwarded-For.
	     */
	    goto done;
	}
	clientFollowXForwardedForNext(http);
	return;
    } else if (answer == ACCESS_DENIED) {
	debug(33, 5) ("clientFollowXForwardedForDone: indirect_client_addr=%s not trusted\n",
	    inet_ntoa(request->indirect_client_addr));
    } else {
	debug(33, 5) ("clientFollowXForwardedForDone: indirect_client_addr=%s nothing more to do\n",
	    inet_ntoa(request->indirect_client_addr));
    }
  done:
    /* clean up, and pass control to clientAccessCheck */
    debug(33, 6) ("clientFollowXForwardedForDone: cleanup\n");
    if (Config.onoff.log_uses_indirect_client) {
	/*
	 * Ensure that the access log shows the indirect client
	 * instead of the direct client.
	 */
	ConnStateData *conn = http->conn;
	conn->log_addr = request->indirect_client_addr;
	conn->log_addr.s_addr &= Config.Addrs.client_netmask.s_addr;
	debug(33, 3) ("clientFollowXForwardedForDone: setting log_addr=%s\n",
	    inet_ntoa(conn->log_addr));
    }
    stringClean(&request->x_forwarded_for_iterator);
    http->acl_checklist = NULL;	/* XXX do we need to aclChecklistFree() ? */
    clientAccessCheck(http);
}
#endif /* FOLLOW_X_FORWARDED_FOR */

void
clientCheckFollowXForwardedFor(void *data)
{
    clientHttpRequest *http = data;
#if FOLLOW_X_FORWARDED_FOR
    if (Config.accessList.followXFF && httpHeaderHas(&http->request->header, HDR_X_FORWARDED_FOR)) {
	clientFollowXForwardedForStart(http);
	return;
    }
#endif
    clientAccessCheck(http);
}
