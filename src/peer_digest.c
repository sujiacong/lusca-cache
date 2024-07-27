
/*
 * $Id: peer_digest.c 13778 2009-02-02 18:19:02Z adrian.chadd $
 *
 * DEBUG: section 72    Peer Digest Routines
 * AUTHOR: Alex Rousskov
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

#if USE_CACHE_DIGESTS

/* local types */

/* local prototypes */
static time_t peerDigestIncDelay(const PeerDigest * pd);
static time_t peerDigestNewDelay(const StoreEntry * e);
static void peerDigestSetCheck(PeerDigest * pd, time_t delay);
static void peerDigestClean(PeerDigest *);
static EVH peerDigestCheck;
static void peerDigestRequest(PeerDigest * pd);
static STNCB peerDigestFetchReply;
static STNCB peerDigestSwapInHeaders;
static STNCB peerDigestSwapInCBlock;
static STNCB peerDigestSwapInMask;
static int peerDigestFetchedEnough(DigestFetchState * fetch, ssize_t size, const char *step_name);
static void peerDigestFetchStop(DigestFetchState * fetch, const char *reason);
static void peerDigestFetchAbort(DigestFetchState * fetch, const char *reason);
static void peerDigestReqFinish(DigestFetchState * fetch, int, int, int, const char *reason, int err);
static void peerDigestPDFinish(DigestFetchState * fetch, int pcb_valid, int err);
static void peerDigestFetchFinish(DigestFetchState * fetch, int err);
static void peerDigestFetchSetStats(DigestFetchState * fetch);
static int peerDigestSetCBlock(PeerDigest * pd, const char *buf);
static int peerDigestUseful(const PeerDigest * pd);

MemPool * pool_cache_digest = NULL;

/* local constants */

#define StoreDigestCBlockSize sizeof(StoreDigestCBlock)

/* min interval for requesting digests from a given peer */
static const time_t PeerDigestReqMinGap = 5 * 60;	/* seconds */
/* min interval for requesting digests (cumulative request stream) */
static const time_t GlobDigestReqMinGap = 1 * 60;	/* seconds */

/* local vars */

static time_t pd_last_req_time = 0;	/* last call to Check */

/* initialize peer digest */
void
peerDigestInitMem(void)
{
    pool_cache_digest = memPoolCreate("CacheDigest", sizeof(CacheDigest));
}

static void
peerDigestInit(PeerDigest * pd, peer * p)
{
    assert(pd && p);

    memset(pd, 0, sizeof(*pd));
    pd->peer = p;
    /* if peer disappears, we will know it's name */
    stringInit(&pd->host, p->host);

    pd->times.initialized = squid_curtime;
}

static void
peerDigestClean(PeerDigest * pd)
{
    assert(pd);
    if (pd->cd)
	cacheDigestDestroy(pd->cd);
    stringClean(&pd->host);
}

CBDATA_TYPE(PeerDigest);

/* allocate new peer digest, call Init, and lock everything */
PeerDigest *
peerDigestCreate(peer * p)
{
    PeerDigest *pd;
    assert(p);

    CBDATA_INIT_TYPE(PeerDigest);
    pd = cbdataAlloc(PeerDigest);
    peerDigestInit(pd, p);
    cbdataLock(pd->peer);	/* we will use the peer */

    return pd;
}

/* call Clean and free/unlock everything */
static void
peerDigestDestroy(PeerDigest * pd)
{
    peer *p;
    assert(pd);

    p = pd->peer;
    pd->peer = NULL;
    /* inform peer (if any) that we are gone */
    if (cbdataValid(p))
	peerNoteDigestGone(p);
    cbdataUnlock(p);		/* must unlock, valid or not */

    peerDigestClean(pd);
    cbdataFree(pd);
}

/* called by peer to indicate that somebody actually needs this digest */
void
peerDigestNeeded(PeerDigest * pd)
{
    assert(pd);
    assert(!pd->flags.needed);
    assert(!pd->cd);

    pd->flags.needed = 1;
    pd->times.needed = squid_curtime;
    peerDigestSetCheck(pd, 0);	/* check asap */
}

/* currently we do not have a reason to disable without destroying */
#if FUTURE_CODE
/* disables peer for good */
static void
peerDigestDisable(PeerDigest * pd)
{
    debugs(72, 2, "peerDigestDisable: peer %.*s disabled for good", strLen2(pd->host), strBuf2(pd->host));
    pd->times.disabled = squid_curtime;
    pd->times.next_check = -1;	/* never */
    pd->flags.usable = 0;

    if (pd->cd) {
	cacheDigestDestroy(pd->cd);
	pd->cd = NULL;
    }
    /* we do not destroy the pd itself to preserve its "history" and stats */
}
#endif

/* increment retry delay [after an unsuccessful attempt] */
static time_t
peerDigestIncDelay(const PeerDigest * pd)
{
    assert(pd);
    return pd->times.retry_delay > 0 ?
	2 * pd->times.retry_delay :	/* exponential backoff */
	PeerDigestReqMinGap;	/* minimal delay */
}

/* artificially increases Expires: setting to avoid race conditions 
 * returns the delay till that [increased] expiration time */
static time_t
peerDigestNewDelay(const StoreEntry * e)
{
    assert(e);
    if (e->expires > 0)
	return e->expires + PeerDigestReqMinGap - squid_curtime;
    return PeerDigestReqMinGap;
}

/* registers next digest verification */
static void
peerDigestSetCheck(PeerDigest * pd, time_t delay)
{
    eventAdd("peerDigestCheck", peerDigestCheck, pd, (double) delay, 1);
    pd->times.next_check = squid_curtime + delay;
    debugs(72, 3, "peerDigestSetCheck: will check peer %.*s in %d secs", strLen2(pd->host), strBuf2(pd->host), (int) delay);
}

/*
 * called when peer is about to disappear or have already disappeared
 */
void
peerDigestNotePeerGone(PeerDigest * pd)
{
    if (pd->flags.requested) {
	debugs(72, 2, "peerDigestNotePeerGone: peer %.*s gone, will destroy after fetch.", strLen2(pd->host), strBuf2(pd->host));
	/* do nothing now, the fetching chain will notice and take action */
    } else {
	debugs(72, 2, "peerDigestNotePeerGone: peer %.*s is gone, destroying now.", strLen2(pd->host), strBuf2(pd->host));
	peerDigestDestroy(pd);
    }
}

/* callback for eventAdd() (with peer digest locked)
 * request new digest if our copy is too old or if we lack one; 
 * schedule next check otherwise */
static void
peerDigestCheck(void *data)
{
    PeerDigest *pd = data;
    time_t req_time;

    /*
     * you can't assert(cbdataValid(pd)) -- if its not valid this
     * function never gets called
     */
    assert(!pd->flags.requested);

    pd->times.next_check = 0;	/* unknown */

    if (!cbdataValid(pd->peer)) {
	peerDigestNotePeerGone(pd);
	return;
    }
    debugs(72, 3, "peerDigestCheck: peer %s:%d", pd->peer->host, pd->peer->http_port);
    debugs(72, 3, "peerDigestCheck: time: %ld, last received: %ld (%+d)",
	(long int) squid_curtime, (long int) pd->times.received, (int) (squid_curtime - pd->times.received));

    /* decide when we should send the request:
     * request now unless too close to other requests */
    req_time = squid_curtime;

    /* per-peer limit */
    if (req_time - pd->times.received < PeerDigestReqMinGap) {
	debugs(72, 2, "peerDigestCheck: %.*s, avoiding close peer requests (%d < %d secs).",
	    strLen2(pd->host), strBuf2(pd->host), (int) (req_time - pd->times.received),
	    (int) PeerDigestReqMinGap);
	req_time = pd->times.received + PeerDigestReqMinGap;
    }
    /* global limit */
    if (req_time - pd_last_req_time < GlobDigestReqMinGap) {
	debugs(72, 2, "peerDigestCheck: %.*s, avoiding close requests (%d < %d secs).",
	    strLen2(pd->host), strBuf2(pd->host), (int) (req_time - pd_last_req_time),
	    (int) GlobDigestReqMinGap);
	req_time = pd_last_req_time + GlobDigestReqMinGap;
    }
    if (req_time <= squid_curtime)
	peerDigestRequest(pd);	/* will set pd->flags.requested */
    else
	peerDigestSetCheck(pd, req_time - squid_curtime);
}

CBDATA_TYPE(DigestFetchState);

/* ask store for a digest */
static void
peerDigestRequest(PeerDigest * pd)
{
    peer *p = pd->peer;
    StoreEntry *e, *old_e;
    char *url;
    const cache_key *key;
    request_t *req;
    DigestFetchState *fetch = NULL;

    pd->req_result = NULL;
    pd->flags.requested = 1;

    /* compute future request components */
    if (p->digest_url)
	url = xstrdup(p->digest_url);
    else
	url = internalRemoteUri(p->host, p->http_port,
	    "/squid-internal-periodic/", StoreDigestFileName);

    req = urlParse(urlMethodGetKnownByCode(METHOD_GET), url);
    assert(req);
    key = storeKeyPublicByRequest(req);
    debugs(72, 2, "peerDigestRequest: %s key: %s", url, storeKeyText(key));

    /* add custom headers */
    assert(!req->header.len);
    httpHeaderPutStr(&req->header, HDR_ACCEPT, StoreDigestMimeStr);
    httpHeaderPutStr(&req->header, HDR_ACCEPT, "text/html");
    if (p->login)
	xstrncpy(req->login, p->login, MAX_LOGIN_SZ);
    /* create fetch state structure */
    CBDATA_INIT_TYPE(DigestFetchState);
    fetch = cbdataAlloc(DigestFetchState);
    fetch->request = requestLink(req);
    fetch->pd = pd;
    fetch->offset = 0;

    /* update timestamps */
    fetch->start_time = squid_curtime;
    pd->times.requested = squid_curtime;
    pd_last_req_time = squid_curtime;

    req->flags.cachable = 1;
    /* the rest is based on clientProcessExpired() */
    req->flags.refresh = 1;
    old_e = fetch->old_entry = storeGet(key);
    if (old_e) {
	debugs(72, 5, "peerDigestRequest: found old entry");
	storeLockObject(old_e);
	storeCreateMemObject(old_e, url);
	fetch->old_sc = storeClientRegister(old_e, fetch);
    }
    e = fetch->entry = storeCreateEntry(url, req->flags, req->method);
    assert(EBIT_TEST(e->flags, KEY_PRIVATE));
    fetch->sc = storeClientRegister(e, fetch);
    /* set lastmod to trigger IMS request if possible */
    if (old_e)
	e->lastmod = old_e->lastmod;

    /* push towards peer cache */
    debugs(72, 3, "peerDigestRequest: forwarding to fwdStart...");
    fwdStart(-1, e, req);
    cbdataLock(fetch->pd);
    storeClientRef(fetch->sc, e, 0, 0, SM_PAGE_SIZE, peerDigestFetchReply, fetch);
}

/* wait for full http headers to be received */
static void
peerDigestFetchReply(void *data, mem_node_ref nr, ssize_t size)
{
    const char *buf = NULL;
    DigestFetchState *fetch = data;
    PeerDigest *pd = fetch->pd;
    http_status status;
    HttpReply *reply;
    assert(pd);
    assert(!fetch->offset);

    if (nr.node) {
        assert(size <= nr.node->len - nr.offset);
        buf = nr.node->data + nr.offset;
    }

    if (peerDigestFetchedEnough(fetch, size, "peerDigestFetchReply"))
	goto finish;

    reply = fetch->entry->mem_obj->reply;
    assert(reply);
    status = reply->sline.status;
    debugs(72, 3, "peerDigestFetchReply: %.*s status: %d, expires: %ld (%+d)",
	strLen2(pd->host), strBuf2(pd->host), status,
	(long int) reply->expires, (int) (reply->expires - squid_curtime));

    /* this "if" is based on clientHandleIMSReply() */
    if (status == HTTP_NOT_MODIFIED) {
	request_t *r = NULL;
	/* our old entry is fine */
	assert(fetch->old_entry);
	if (!fetch->old_entry->mem_obj->request)
	    fetch->old_entry->mem_obj->request = r =
		requestLink(fetch->entry->mem_obj->request);
	assert(fetch->old_entry->mem_obj->request);
	httpReplyUpdateOnNotModified(fetch->old_entry->mem_obj->reply, reply);
	storeTimestampsSet(fetch->old_entry);
	/* get rid of 304 reply */
	storeClientUnregister(fetch->sc, fetch->entry, fetch);
	storeUnlockObject(fetch->entry);
	/* And prepare to swap in old entry if needed */
	fetch->entry = fetch->old_entry;
	fetch->old_entry = NULL;
	fetch->sc = fetch->old_sc;
	fetch->old_sc = NULL;
	/* preserve request -- we need its size to update counters */
	/* requestUnlink(r); */
	/* fetch->entry->mem_obj->request = NULL; */
    } else if (status == HTTP_OK) {
	/* get rid of old entry if any */
	if (fetch->old_entry) {
	    debugs(72, 3, "peerDigestFetchReply: got new digest, releasing old one");
	    storeClientUnregister(fetch->old_sc, fetch->old_entry, fetch);
	    storeReleaseRequest(fetch->old_entry);
	    storeUnlockObject(fetch->old_entry);
	    fetch->old_entry = NULL;
	}
    } else {
	/* some kind of a bug */
	peerDigestFetchAbort(fetch, httpStatusLineReason(&reply->sline));
	goto finish;
    }
    /* must have a ready-to-use store entry if we got here */
    /* can we stay with the old in-memory digest? */
    if (status == HTTP_NOT_MODIFIED && fetch->pd->cd)
	peerDigestFetchStop(fetch, "Not modified");
    else
	storeClientRef(fetch->sc, fetch->entry,		/* have to swap in */
	    0, 0, SM_PAGE_SIZE, peerDigestSwapInHeaders, fetch);
  finish:
    stmemNodeUnref(&nr);
}

/* fetch headers from disk, pass on to SwapInCBlock */
static void
peerDigestSwapInHeaders(void *data, mem_node_ref nr, ssize_t size)
{
    DigestFetchState *fetch = data;
    assert(size <= nr.node->len - nr.offset);

    if (peerDigestFetchedEnough(fetch, size, "peerDigestSwapInHeaders"))
	goto finish;

    assert(!fetch->offset);
    assert(fetch->entry->mem_obj->reply);
    if (fetch->entry->mem_obj->reply->sline.status != HTTP_OK) {
	debugs(72, 1, "peerDigestSwapInHeaders: %.*s status %d got cached!",
	    strLen2(fetch->pd->host), strBuf2(fetch->pd->host), fetch->entry->mem_obj->reply->sline.status);
	peerDigestFetchAbort(fetch, "internal status error");
	goto finish;
    }
    fetch->offset = fetch->entry->mem_obj->reply->hdr_sz;
    fetch->buf = memAllocate(MEM_4K_BUF);
    storeClientRef(fetch->sc, fetch->entry, fetch->offset, fetch->offset,
	SM_PAGE_SIZE, peerDigestSwapInCBlock, fetch);
  finish:
    stmemNodeUnref(&nr);
}

static void
peerDigestSwapInCBlock(void *data, mem_node_ref nr, ssize_t size)
{
    const char *buf = nr.node->data + nr.offset;
    DigestFetchState *fetch = data;

    if (peerDigestFetchedEnough(fetch, size, "peerDigestSwapInCBlock"))
	goto finish;

    fetch->offset += size;
    memcpy(fetch->buf + fetch->buf_used, buf, size);
    fetch->buf_used += size;

    if (fetch->buf_used >= StoreDigestCBlockSize) {
	PeerDigest *pd = fetch->pd;
	HttpReply *rep = fetch->entry->mem_obj->reply;

	assert(pd && rep);
	if (peerDigestSetCBlock(pd, fetch->buf)) {
	    /* XXX: soon we will have variable header size */
	    fetch->offset -= fetch->buf_used - StoreDigestCBlockSize;
	    /* switch to CD buffer and fetch digest guts */
	    memFree(fetch->buf, MEM_4K_BUF);
	    fetch->buf = NULL;
	    fetch->buf_used = 0;
	    assert(pd->cd->mask);
	    storeClientRef(fetch->sc, fetch->entry,
		fetch->offset,
		fetch->offset,
		pd->cd->mask_size,
		peerDigestSwapInMask, fetch);
	} else {
	    peerDigestFetchAbort(fetch, "invalid digest cblock");
	}
    } else {
	/* need more data, do we have space? */
	if (fetch->buf_used >= SM_PAGE_SIZE)
	    peerDigestFetchAbort(fetch, "digest cblock too big");
	else
	    storeClientRef(fetch->sc, fetch->entry, fetch->offset, fetch->offset, SM_PAGE_SIZE - fetch->buf_used,
		peerDigestSwapInCBlock, fetch);
    }
  finish:
    stmemNodeUnref(&nr);
}

static void
peerDigestSwapInMask(void *data, mem_node_ref nr, ssize_t size)
{
    const char *buf = nr.node->data + nr.offset;
    DigestFetchState *fetch = data;
    PeerDigest *pd;
    assert(size <= nr.node->len - nr.offset);

    if (peerDigestFetchedEnough(fetch, size, "peerDigestSwapInMask")) {
	stmemNodeUnref(&nr);
	return;
    }
    pd = fetch->pd;
    assert(pd->cd && pd->cd->mask);

    /* Copy data into the peer digest mask */
    if (size > 0) {
        /* clamp the data fetched to only be the left over data for the mask; there may be more data! */
	if (size + fetch->mask_offset > pd->cd->mask_size)
		size = pd->cd->mask_size - fetch->mask_offset;
	/* assert(size + fetch->mask_offset < pd->cd->mask_size); */
	memcpy(pd->cd->mask + fetch->mask_offset, buf, size);
    }
    stmemNodeUnref(&nr);

    fetch->offset += size;
    fetch->mask_offset += size;
    if (fetch->mask_offset >= pd->cd->mask_size) {
	debugs(72, 2, "peerDigestSwapInMask: Done! Got %" PRINTF_OFF_T ", expected %d",
	    fetch->mask_offset, pd->cd->mask_size);
	assert(fetch->mask_offset == pd->cd->mask_size);
	assert(peerDigestFetchedEnough(fetch, 0, "peerDigestSwapInMask"));
    } else {
	const size_t buf_sz = pd->cd->mask_size - fetch->mask_offset;
	assert(buf_sz > 0);
	storeClientRef(fetch->sc, fetch->entry,
	    fetch->offset,
	    fetch->offset,
	    buf_sz,
	    peerDigestSwapInMask, fetch);
    }
}

static int
peerDigestFetchedEnough(DigestFetchState * fetch, ssize_t size, const char *step_name)
{
    PeerDigest *pd = NULL;
    const char *host_str = NULL;	/* peer host */
    const char *reason = NULL;	/* reason for completion */
    const char *no_bug = NULL;	/* successful completion if set */
    const int fcb_valid = cbdataValid(fetch);
    const int pdcb_valid = fcb_valid && cbdataValid(fetch->pd);
    const int pcb_valid = pdcb_valid && cbdataValid(fetch->pd->peer);

    /* test possible exiting conditions (the same for most steps!)
     * cases marked with '?!' should not happen */

    if (!reason) {
	if (!fcb_valid)
	    reason = "fetch aborted?!";
	else if (!(pd = fetch->pd))
	    reason = "peer digest disappeared?!";
#if DONT
	else if (!cbdataValid(pd))
	    reason = "invalidated peer digest?!";
#endif
	else
	    host_str = stringDupToC(&pd->host);
    }
    debugs(72, 6, "%s: peer %s, offset: %" PRINTF_OFF_T " size: %d.",
	step_name, host_str ? host_str : "<unknown>", fcb_valid ? fetch->offset : (squid_off_t) - 1, (int) size);

    /* continue checking (with pd and host known and valid) */
    if (!reason) {
	if (!cbdataValid(pd->peer))
	    reason = "peer disappeared";
	else if (size < 0)
	    reason = "swap failure";
	else if (!fetch->entry)
	    reason = "swap aborted?!";
	else if (EBIT_TEST(fetch->entry->flags, ENTRY_ABORTED))
	    reason = "swap aborted";
    }
    /* continue checking (maybe-successful eof case) */
    if (!reason && !size) {
	if (!pd->cd)
	    reason = "null digest?!";
	else if (fetch->buf)
	    reason = "premature end of digest header?!";
	else if (fetch->mask_offset != pd->cd->mask_size)
	    reason = "premature end of digest mask?!";
	else if (!peerDigestUseful(pd))
	    reason = "useless digest";
	else
	    reason = no_bug = "success";
    }
    /* finish if we have a reason */
    if (reason) {
	const int level = strstr(reason, "?!") ? 1 : 3;
	debugs(72, level, "%s: peer %s, exiting after '%s'",
	    step_name, host_str ? host_str : "<unknown>", reason);
	peerDigestReqFinish(fetch, fcb_valid, pdcb_valid, pcb_valid, reason, !no_bug);
    } else {
	/* paranoid check */
	assert(fcb_valid && pdcb_valid && pcb_valid);
    }
    safe_free(host_str);
    return reason != NULL;
}

/* call this when all callback data is valid and fetch must be stopped but
 * no error has occurred (e.g. we received 304 reply and reuse old digest) */
static void
peerDigestFetchStop(DigestFetchState * fetch, const char *reason)
{
    assert(reason);
    debugs(72, 2, "peerDigestFetchStop: peer %.*s, reason: %s",
	strLen2(fetch->pd->host), strBuf2(fetch->pd->host), reason);
    peerDigestReqFinish(fetch, 1, 1, 1, reason, 0);
}

/* call this when all callback data is valid but something bad happened */
static void
peerDigestFetchAbort(DigestFetchState * fetch, const char *reason)
{
    assert(reason);
    debugs(72, 2, "peerDigestFetchAbort: peer %.*s, reason: %s",
	strLen2(fetch->pd->host), strBuf2(fetch->pd->host), reason);
    peerDigestReqFinish(fetch, 1, 1, 1, reason, 1);
}

/* complete the digest transfer, update stats, unlock/release everything */
static void
peerDigestReqFinish(DigestFetchState * fetch,
    int fcb_valid, int pdcb_valid, int pcb_valid,
    const char *reason, int err)
{
    assert(reason);

    /* must go before peerDigestPDFinish */
    if (pdcb_valid) {
	fetch->pd->flags.requested = 0;
	fetch->pd->req_result = reason;
    }
    /* schedule next check if peer is still out there */
    if (pcb_valid) {
	PeerDigest *pd = fetch->pd;
	if (err) {
	    pd->times.retry_delay = peerDigestIncDelay(pd);
	    peerDigestSetCheck(pd, pd->times.retry_delay);
	} else {
	    pd->times.retry_delay = 0;
	    peerDigestSetCheck(pd, peerDigestNewDelay(fetch->entry));
	}
    }
    /* note: order is significant */
    if (fcb_valid)
	peerDigestFetchSetStats(fetch);
    if (pdcb_valid)
	peerDigestPDFinish(fetch, pcb_valid, err);
    if (fcb_valid)
	peerDigestFetchFinish(fetch, err);
}


/* destroys digest if peer disappeared
 * must be called only when fetch and pd cbdata are valid */
static void
peerDigestPDFinish(DigestFetchState * fetch, int pcb_valid, int err)
{
    PeerDigest *pd = fetch->pd;

    pd->times.received = squid_curtime;
    pd->times.req_delay = fetch->resp_time;
    kb_incr(&pd->stats.sent.kbytes, (size_t) fetch->sent.bytes);
    kb_incr(&pd->stats.recv.kbytes, (size_t) fetch->recv.bytes);
    pd->stats.sent.msgs += fetch->sent.msg;
    pd->stats.recv.msgs += fetch->recv.msg;

    if (err) {
	debugs(72, 1, "%sdisabling (%s) digest from %.*s",
	    pcb_valid ? "temporary " : "",
	    pd->req_result, strLen2(pd->host), strBuf2(pd->host));

	if (pd->cd) {
	    cacheDigestDestroy(pd->cd);
	    pd->cd = NULL;
	}
	pd->flags.usable = 0;

	if (!pcb_valid)
	    peerDigestNotePeerGone(pd);
    } else {
	assert(pcb_valid);

	pd->flags.usable = 1;

	/* XXX: ugly condition, but how? */
	if (fetch->entry->store_status == STORE_OK)
	    debugs(72, 2, "re-used old digest from %.*s", strLen2(pd->host), strBuf2(pd->host));
	else
	    debugs(72, 2, "received valid digest from %.*s", strLen2(pd->host), strBuf2(pd->host));
    }
    fetch->pd = NULL;
    cbdataUnlock(pd);
}

/* free fetch state structures
 * must be called only when fetch cbdata is valid */
static void
peerDigestFetchFinish(DigestFetchState * fetch, int err)
{
    assert(fetch->entry && fetch->request);

    if (fetch->old_entry) {
	debugs(72, 2, "peerDigestFetchFinish: deleting old entry");
	storeClientUnregister(fetch->old_sc, fetch->old_entry, fetch);
	storeReleaseRequest(fetch->old_entry);
	storeUnlockObject(fetch->old_entry);
	fetch->old_entry = NULL;
    }
    /* update global stats */
    kb_incr(&statCounter.cd.kbytes_sent, (size_t) fetch->sent.bytes);
    kb_incr(&statCounter.cd.kbytes_recv, (size_t) fetch->recv.bytes);
    statCounter.cd.msgs_sent += fetch->sent.msg;
    statCounter.cd.msgs_recv += fetch->recv.msg;

    /* unlock everything */
    storeClientUnregister(fetch->sc, fetch->entry, fetch);
    storeUnlockObject(fetch->entry);
    requestUnlink(fetch->request);
    fetch->entry = NULL;
    fetch->request = NULL;
    assert(fetch->pd == NULL);
    if (fetch->buf) {
	memFree(fetch->buf, MEM_4K_BUF);
	fetch->buf = NULL;
    }
    cbdataFree(fetch);
}

/* calculate fetch stats after completion */
static void
peerDigestFetchSetStats(DigestFetchState * fetch)
{
    MemObject *mem;
    assert(fetch->entry && fetch->request);

    mem = fetch->entry->mem_obj;
    assert(mem);

    /* XXX: outgoing numbers are not precise */
    /* XXX: we must distinguish between 304 hits and misses here */
    fetch->sent.bytes = httpRequestPrefixLen(fetch->request);
    fetch->recv.bytes = fetch->entry->store_status == STORE_PENDING ?
	mem->inmem_hi : mem->object_sz;
    fetch->sent.msg = fetch->recv.msg = 1;
    fetch->expires = fetch->entry->expires;
    fetch->resp_time = squid_curtime - fetch->start_time;

    debugs(72, 3, "peerDigestFetchSetStats: recv %d bytes in %d secs",
	fetch->recv.bytes, (int) fetch->resp_time);
    debugs(72, 3, "peerDigestFetchSetStats: expires: %ld (%+d), lmt: %ld (%+d)",
	(long int) fetch->expires, (int) (fetch->expires - squid_curtime),
	(long int) fetch->entry->lastmod, (int) (fetch->entry->lastmod - squid_curtime));
}


static int
peerDigestSetCBlock(PeerDigest * pd, const char *buf)
{
    StoreDigestCBlock cblock;
    int freed_size = 0;
    int rval = 0;
    const char *host;

    host = stringDupToC(&pd->host);

    xmemcpy(&cblock, buf, sizeof(cblock));
    /* network -> host conversions */
    cblock.ver.current = ntohs(cblock.ver.current);
    cblock.ver.required = ntohs(cblock.ver.required);
    cblock.capacity = ntohl(cblock.capacity);
    cblock.count = ntohl(cblock.count);
    cblock.del_count = ntohl(cblock.del_count);
    cblock.mask_size = ntohl(cblock.mask_size);
    debugs(72, 2, "got digest cblock from %s; ver: %d (req: %d)",
	host, (int) cblock.ver.current, (int) cblock.ver.required);
    debugs(72, 2, "\t size: %d bytes, e-cnt: %d, e-util: %d%%",
	cblock.mask_size, cblock.count,
	xpercentInt(cblock.count, cblock.capacity));
    /* check version requirements (both ways) */
    if (cblock.ver.required > CacheDigestVer.current) {
	debugs(72, 1, "%s digest requires version %d; have: %d",
	    host, cblock.ver.required, CacheDigestVer.current);
	rval = 0;
        goto finish;
    }
    if (cblock.ver.current < CacheDigestVer.required) {
	debugs(72, 1, "%s digest is version %d; we require: %d",
	    host, cblock.ver.current, CacheDigestVer.required);
 	rval = 0;
	goto finish;
    }
    /* check consistency */
    if (cblock.ver.required > cblock.ver.current ||
	cblock.mask_size <= 0 || cblock.capacity <= 0 ||
	cblock.bits_per_entry <= 0 || cblock.hash_func_count <= 0) {
	debugs(72, 0, "%s digest cblock is corrupted.", host);
	rval = 0;
	goto finish;
    }
    /* check consistency further */
    if (cblock.mask_size != cacheDigestCalcMaskSize(cblock.capacity, cblock.bits_per_entry)) {
	debugs(72, 0, "%s digest cblock is corrupted (mask size mismatch: %d ? %d).",
	    host, cblock.mask_size, (int) cacheDigestCalcMaskSize(cblock.capacity, cblock.bits_per_entry));
	rval = 0;
	goto finish;
    }
    /* there are some things we cannot do yet */
    if (cblock.hash_func_count != CacheDigestHashFuncCount) {
	debugs(72, 0, "%s digest: unsupported #hash functions: %d ? %d.",
	    host, cblock.hash_func_count, CacheDigestHashFuncCount);
	rval = 0;
	goto finish;
    }
    /*
     * no cblock bugs below this point
     */
    /* check size changes */
    if (pd->cd && cblock.mask_size != pd->cd->mask_size) {
	debugs(72, 2, "%s digest changed size: %d -> %d",
	    host, cblock.mask_size, pd->cd->mask_size);
	freed_size = pd->cd->mask_size;
	cacheDigestDestroy(pd->cd);
	pd->cd = NULL;
    }
    if (!pd->cd) {
	debugs(72, 2, "creating %s digest; size: %d (%+d) bytes",
	    host, cblock.mask_size, (int) (cblock.mask_size - freed_size));
	pd->cd = cacheDigestCreate(cblock.capacity, cblock.bits_per_entry);
	if (cblock.mask_size >= freed_size)
	    kb_incr(&statCounter.cd.memory, cblock.mask_size - freed_size);
    }
    assert(pd->cd);
    /* these assignments leave us in an inconsistent state until we finish reading the digest */
    pd->cd->count = cblock.count;
    pd->cd->del_count = cblock.del_count;
    rval = 1;
finish:
    safe_free(host);
    return rval;
}

static int
peerDigestUseful(const PeerDigest * pd)
{
    /* TODO: we should calculate the prob of a false hit instead of bit util */
    const int bit_util = cacheDigestBitUtil(pd->cd);
    if (bit_util > 65) {
	debugs(72, 0, "Warning: %.*s peer digest has too many bits on (%d%%).",
	    strLen2(pd->host), strBuf2(pd->host), bit_util);
	return 0;
    }
    return 1;
}

static int
saneDiff(time_t diff)
{
    return abs(diff) > squid_curtime / 2 ? 0 : diff;
}

void
peerDigestStatsReport(const PeerDigest * pd, StoreEntry * e)
{
    char *host = NULL;

#define f2s(flag) (pd->flags.flag ? "yes" : "no")
#define appendTime(tm) storeAppendPrintf(e, "%s\t %10ld\t %+d\t %+d\n", \
    ""#tm, (long int)pd->times.tm, \
    saneDiff(pd->times.tm - squid_curtime), \
    saneDiff(pd->times.tm - pd->times.initialized))

    assert(pd);

    host = stringDupToC(&pd->host);

    storeAppendPrintf(e, "\npeer digest from %s\n", host);

    cacheDigestGuessStatsReport(&pd->stats.guess, e, host);

    storeAppendPrintf(e, "\nevent\t timestamp\t secs from now\t secs from init\n");
    appendTime(initialized);
    appendTime(needed);
    appendTime(requested);
    appendTime(received);
    appendTime(next_check);

    storeAppendPrintf(e, "peer digest state:\n");
    storeAppendPrintf(e, "\tneeded: %3s, usable: %3s, requested: %3s\n",
	f2s(needed), f2s(usable), f2s(requested));
    storeAppendPrintf(e, "\n\tlast retry delay: %d secs\n",
	(int) pd->times.retry_delay);
    storeAppendPrintf(e, "\tlast request response time: %d secs\n",
	(int) pd->times.req_delay);
    storeAppendPrintf(e, "\tlast request result: %s\n",
	pd->req_result ? pd->req_result : "(none)");

    storeAppendPrintf(e, "\npeer digest traffic:\n");
    storeAppendPrintf(e, "\trequests sent: %d, volume: %d KB\n",
	pd->stats.sent.msgs, (int) pd->stats.sent.kbytes.kb);
    storeAppendPrintf(e, "\treplies recv:  %d, volume: %d KB\n",
	pd->stats.recv.msgs, (int) pd->stats.recv.kbytes.kb);

    storeAppendPrintf(e, "\npeer digest structure:\n");
    if (pd->cd)
	cacheDigestReport(pd->cd, host, e);
    else
	storeAppendPrintf(e, "\tno in-memory copy\n");

    safe_free(host);
}

#endif
