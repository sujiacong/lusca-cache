
/*
 * $Id: peer_monitor.c 13698 2009-01-17 20:15:04Z adrian.chadd $
 *
 * DEBUG: section ??    Peer monitoring
 * AUTHOR: Henrik Nordstrom
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

#define DBG	1
#include "squid.h"

/* local types */

struct _PeerMonitor {
    peer *peer;
    time_t last_probe;
    struct {
	request_t *req;
	StoreEntry *e;
	store_client *sc;
	int size;
	http_status status;
	int offset;
	int timeout_set;
    } running;
    char name[40];
};

CBDATA_TYPE(PeerMonitor);

static void peerMonitorCompleted(PeerMonitor * pm);

static void
freePeerMonitor(void *data)
{
    PeerMonitor *pm = data;
    if (cbdataValid(pm->peer))
	pm->peer->monitor.data = NULL;
    cbdataUnlock(pm->peer);
    pm->peer = NULL;
}

static void
peerMonitorFetchReply(void *data, mem_node_ref nr, ssize_t size)
{
    PeerMonitor *pm = data;

    if (size <= 0 || !cbdataValid(pm->peer)) {
	peerMonitorCompleted(pm);
    } else {
	pm->running.size += size;
	pm->running.offset += size;
	storeClientRef(pm->running.sc, pm->running.e, pm->running.offset, pm->running.offset, SM_PAGE_SIZE, peerMonitorFetchReply, pm);
    }
    stmemNodeUnref(&nr);
}

static void
peerMonitorFetchReplyHeaders(void *data, mem_node_ref nr, ssize_t size)
{
    PeerMonitor *pm = data;
    const char *buf = NULL;
    http_status status;
    HttpReply *reply;

    if (EBIT_TEST(pm->running.e->flags, ENTRY_ABORTED))
	goto completed;
    if (size <= 0)
	goto completed;
    if (!cbdataValid(pm->peer))
	goto completed;

    buf = nr.node->data + nr.offset;

    reply = pm->running.e->mem_obj->reply;
    assert(reply);
    status = reply->sline.status;
    pm->running.status = status;
    if (status != HTTP_OK)
	goto completed;
    if (size > reply->hdr_sz) {
	pm->running.size = size - reply->hdr_sz;
	pm->running.offset = size;
    } else {
	pm->running.size = 0;
	pm->running.offset = reply->hdr_sz;
    }
    storeClientRef(pm->running.sc, pm->running.e, pm->running.offset, pm->running.offset, SM_PAGE_SIZE, peerMonitorFetchReply, pm);
    stmemNodeUnref(&nr);
    return;

  completed:
    /* We are fully done with this monitoring request. Clean up */
    stmemNodeUnref(&nr);
    peerMonitorCompleted(pm);
    return;
}

static void
peerMonitorTimeout(void *data)
{
    PeerMonitor *pm = data;
    store_client *sc = pm->running.sc;
    pm->running.status = HTTP_REQUEST_TIMEOUT;
    pm->running.sc = NULL;
    pm->running.timeout_set = 0;
    /* This will invoke peerMonitorFetchReplyHeaders which finishes things up */
    storeClientUnregister(sc, pm->running.e, pm);
}

static void
peerMonitorRequest(void *data)
{
    PeerMonitor *pm = data;
    char *url;
    request_t *req;

    if (!cbdataValid(pm->peer)) {
	cbdataFree(pm);
	return;
    }
    url = pm->peer->monitor.url;
    if (!url) {
	cbdataFree(pm);
	return;
    }
    req = urlParse(urlMethodGetKnownByCode(METHOD_GET), url);
    if (!req) {
	debugs(DBG, 1, "peerMonitorRequest: Failed to parse URL '%s' for cache_peer %s", url, pm->peer->name);
	cbdataFree(pm);
	return;
    }
    pm->last_probe = squid_curtime;
    pm->running.timeout_set = 1;
    eventAdd(pm->name, peerMonitorTimeout, pm, (double) (pm->peer->monitor.timeout ? pm->peer->monitor.timeout : pm->peer->monitor.interval), 0);

    httpHeaderPutStr(&req->header, HDR_ACCEPT, "*/*");
    httpHeaderPutStr(&req->header, HDR_USER_AGENT, full_appname_string);
    if (pm->peer->login)
	xstrncpy(req->login, pm->peer->login, MAX_LOGIN_SZ);
    pm->running.req = requestLink(req);
    pm->running.e = storeCreateEntry(url, req->flags, req->method);
    pm->running.sc = storeClientRegister(pm->running.e, pm);
    fwdStartPeer(pm->peer, pm->running.e, pm->running.req);
    storeClientRef(pm->running.sc, pm->running.e, 0, 0, SM_PAGE_SIZE, peerMonitorFetchReplyHeaders, pm);
    return;
}

static void
peerMonitorCompleted(PeerMonitor * pm)
{
    int state = PEER_ALIVE;
    peer *p = pm->peer;
    storeClientUnregister(pm->running.sc, pm->running.e, pm);
    storeUnlockObject(pm->running.e);
    requestUnlink(pm->running.req);
    if (pm->running.timeout_set) {
	eventDelete(peerMonitorTimeout, pm);
	pm->running.timeout_set = 0;
    }
    if (!cbdataValid(pm->peer)) {
	cbdataFree(pm);
	return;
    }
    /* Figure out if the response was OK or not */
    if (pm->running.status != HTTP_OK) {
	debugs(DBG, 1, "peerMonitor %s: Failed, status != 200 (%d)",
	    p->name, pm->running.status);
	state = PEER_DEAD;
    } else if (pm->running.size < p->monitor.min) {
	debugs(DBG, 1, "peerMonitor %s: Failed, reply size %d < min %d",
	    p->name, pm->running.size, p->monitor.min);
	state = PEER_DEAD;
    } else if (pm->running.size > p->monitor.max && p->monitor.max > 0) {
	debugs(DBG, 1, "peerMonitor %s: Failed, reply size %d > max %d",
	    p->name, pm->running.size, p->monitor.max);
	state = PEER_DEAD;
    } else {
	debugs(DBG, 2, "peerMonitor %s: OK", p->name);
    }
    p->monitor.state = state;
    if (state != p->stats.logged_state) {
	switch (state) {
	case PEER_ALIVE:
	    debugs(DBG, 1, "Detected REVIVED %s: %s",
		neighborTypeStr(p), p->name);
	    peerClearRR();
	    break;
	case PEER_DEAD:
	    debugs(DBG, 1, "Detected DEAD %s: %s",
		neighborTypeStr(p), p->name);
	    break;
	}
	p->stats.logged_state = state;
    }
    memset(&pm->running, 0, sizeof(pm->running));
    eventAdd(pm->name, peerMonitorRequest, pm, (double) (pm->last_probe + pm->peer->monitor.interval - current_dtime), 1);
}

static void
peerMonitorStart(peer * peer)
{
    PeerMonitor *pm;
    char *url = peer->monitor.url;
    if (!url || !*url)
	return;
    if (!peer->monitor.interval)
	return;
    CBDATA_INIT_TYPE_FREECB(PeerMonitor, freePeerMonitor);
    pm = cbdataAlloc(PeerMonitor);
    snprintf(pm->name, sizeof(pm->name), "monitor %s", peer->name);
    pm->peer = peer;
    peer->monitor.data = pm;
    cbdataLock(pm->peer);
    peerMonitorRequest(pm);
}

void
peerMonitorNow(peer * peer)
{
    PeerMonitor *pm = peer->monitor.data;
    if (pm && !pm->running.req) {
	eventDelete(peerMonitorRequest, pm);
	peerMonitorRequest(pm);
    }
}

void
peerMonitorInit(void)
{
    peer *p;
    for (p = Config.peers; p; p = p->next)
	peerMonitorStart(p);
}
