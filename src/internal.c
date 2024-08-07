
/*
 * $Id: internal.c 13785 2009-02-02 18:59:28Z adrian.chadd $
 *
 * DEBUG: section 76    Internal Squid Object handling
 * AUTHOR: Duane, Alex, Henrik
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

/* called when we "miss" on an internal object;
 * generate known dynamic objects, 
 * return HTTP_NOT_FOUND for others
 */
void
internalStart(request_t * request, StoreEntry * entry)
{
    ErrorState *err;

    debugs(76, 3, "internalStart: %s requesting '%.*s'",
	inet_ntoa(request->client_addr), strLen2(request->urlpath), strBuf2(request->urlpath));
    if (strCmp(request->urlpath, "/squid-internal-dynamic/netdb") == 0) {
	netdbBinaryExchange(entry);
    } else if (strCmp(request->urlpath, "/squid-internal-periodic/store_digest") == 0) {
#if USE_CACHE_DIGESTS
	const char *msgbuf = "This cache is currently building its digest.\n";
#else
	const char *msgbuf = "This cache does not suport Cache Digests.\n";
#endif
	httpReplySetHeaders(entry->mem_obj->reply, HTTP_NOT_FOUND, "Not Found", "text/plain", strlen(msgbuf), squid_curtime, -1);
	httpReplySwapOut(entry->mem_obj->reply, entry);
	storeAppend(entry, msgbuf, strlen(msgbuf));
	storeComplete(entry);
    } else {
	debugObj(76, 1, "internalStart: unknown request:\n",
	    request, (ObjPackMethod) & httpRequestPackDebug);
	err = errorCon(ERR_INVALID_REQ, HTTP_NOT_FOUND, request);
	errorAppendEntry(entry, err);
    }
}

int
internalCheck(const char *urlpath)
{
    return (0 == strncmp(urlpath, "/squid-internal-", 16));
}

int
internalStaticCheck(const char *urlpath)
{
    return (0 == strncmp(urlpath, "/squid-internal-static", 22));
}

/*
 * makes internal url with a given host and port (remote internal url)
 */
char *
internalRemoteUri(const char *host, u_short port, const char *dir, const char *name)
{
    static MemBuf mb = MemBufNULL;
    static char lc_host[SQUIDHOSTNAMELEN];
    assert(host && name);
    /* convert host name to lower case */
    xstrncpy(lc_host, host, SQUIDHOSTNAMELEN);
    Tolower(lc_host);
    /*
     * append the domain in order to mirror the requests with appended
     * domains
     */
    if (Config.appendDomain && !strchr(lc_host, '.'))
	strncat(lc_host, Config.appendDomain, SQUIDHOSTNAMELEN -
	    strlen(lc_host) - 1);
    /* build uri in mb */
    memBufReset(&mb);
    memBufPrintf(&mb, "http://%s", lc_host);
    /* append port if not default */
    if (port && port != urlDefaultPort(PROTO_HTTP))
	memBufPrintf(&mb, ":%d", port);
    if (dir)
	memBufPrintf(&mb, "%s", dir);
    memBufPrintf(&mb, "%s", name);
    /* return a pointer to a local static buffer */
    return mb.buf;
}

/*
 * makes internal url with local host and port
 */
char *
internalLocalUri(const char *dir, const char *name)
{
    return internalRemoteUri(internalHostname(),
	getMyPort(), dir, name);
}

static const char *
internalUniqueHostname(void)
{
    LOCAL_ARRAY(char, host, SQUIDHOSTNAMELEN + 1);
    xstrncpy(host, uniqueHostname(), SQUIDHOSTNAMELEN);
    if (Config.appendDomain && !strchr(host, '.'))
	strncat(host, Config.appendDomain, SQUIDHOSTNAMELEN -
	    strlen(host) - 1);
    Tolower(host);
    return host;
}

/*
 * makes internal url for store
 */
char *
internalStoreUri(const char *dir, const char *name)
{
    static MemBuf mb = MemBufNULL;
    memBufReset(&mb);
    memBufPrintf(&mb, "internal://%s%s%s", internalUniqueHostname(), dir ? dir : "", name);
    return mb.buf;
}

const char *
internalHostname(void)
{
    LOCAL_ARRAY(char, host, SQUIDHOSTNAMELEN + 1);
    xstrncpy(host, getMyHostname(), SQUIDHOSTNAMELEN);
    if (Config.appendDomain && !strchr(host, '.'))
	strncat(host, Config.appendDomain, SQUIDHOSTNAMELEN -
	    strlen(host) - 1);
    Tolower(host);
    return host;
}

int
internalHostnameIs(const char *arg)
{
    wordlist *w;
    if (0 == strcmp(arg, internalHostname()))
	return 1;
    if (Config.uniqueHostname && 0 == strcmp(arg, internalUniqueHostname()))
	return 1;
    for (w = Config.hostnameAliases; w; w = w->next)
	if (0 == strcmp(arg, w->key))
	    return 1;
    return 0;
}
