
/*
 * $Id: ipcache.c 14601 2010-04-16 05:06:55Z adrian.chadd $
 *
 * DEBUG: section 14    IP Cache
 * AUTHOR: Harvest Derived
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

#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#if HAVE_STRING_H
#include <string.h>
#endif
#include <math.h>
#include <fcntl.h>
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#if HAVE_NETDB_H
#include <netdb.h>
#endif

#include "../include/Array.h"
#include "../include/Stack.h"
#include "../include/util.h"
#include "../include/hash.h"
#include "../include/rfc1035.h"

#include "../libcore/valgrind.h"
#include "../libcore/varargs.h"
#include "../libcore/debug.h"
#include "../libcore/tools.h"
#include "../libcore/dlink.h"
#include "../libcore/gb.h"

#include "../libmem/MemPool.h"
#include "../libmem/MemBufs.h"
#include "../libmem/MemBuf.h"
#include "../libmem/wordlist.h"

#include "../libsqinet/sqinet.h"

#include "../libhelper/ipc.h"
#include "../libhelper/helper.h"

#include "../libcb/cbdata.h"

#include "../libiapp/event.h"

#include "../libstat/StatHist.h"


#include "../libsqdns/dns.h"
#include "../libsqdns/dns_internal.h"

#include "namecfg.h"
#include "ipcache.h"

IpcacheStatStruct IpcacheStats;
dlink_list ipcache_lru_list;
MemPool * pool_ipcache = NULL;

static FREE ipcacheFreeEntry;
static IDNSCB ipcacheHandleReply;
static IPH dummy_handler;
static int ipcacheExpiredEntry(ipcache_entry *);
static int ipcache_testname(wordlist *hostlist);
static ipcache_entry *ipcacheParse(ipcache_entry *, rfc1035_rr *, int, const char *error);
static ipcache_entry *ipcache_get(const char *);
static void ipcacheLockEntry(ipcache_entry *);
static void ipcacheUnlockEntry(ipcache_entry *);
static void ipcacheRelease(ipcache_entry *);

static ipcache_addrs static_addrs;
hash_table *ip_table = NULL;

static long ipcache_low = 180;
static long ipcache_high = 200;

#if LIBRESOLV_DNS_TTL_HACK
extern int _dns_ttl_;
#endif

static int
ipcache_testname(wordlist *hostlist)
{
    wordlist *w = NULL;
    debugs(14, 1, "Performing DNS Tests...");
    if ((w = hostlist) == NULL)
	return 1;
    for (; w; w = w->next) {
	if (gethostbyname(w->key) != NULL)
	    return 1;
    }
    return 0;
}

/* removes the given ipcache entry */
static void
ipcacheRelease(ipcache_entry * i)
{
    debugs(14, 3, "ipcacheRelease: Releasing entry for '%s'", (const char *) i->hash.key);
    hash_remove_link(ip_table, (hash_link *) i);
    dlinkDelete(&i->lru, &ipcache_lru_list);
    ipcacheFreeEntry(i);
}

static ipcache_entry *
ipcache_get(const char *name)
{
    if (ip_table != NULL)
	return (ipcache_entry *) hash_lookup(ip_table, name);
    else
	return NULL;
}

static int
ipcacheExpiredEntry(ipcache_entry * i)
{
    /* all static entries are locked, so this takes care of them too */
    if (i->locks != 0)
	return 0;
    if (i->addrs.count == 0)
	if (0 == i->flags.negcached)
	    return 1;
    if (i->expires > squid_curtime)
	return 0;
    return 1;
}

void
ipcache_purgelru(void *voidnotused)
{
    dlink_node *m;
    dlink_node *prev = NULL;
    ipcache_entry *i;
    int removed = 0;
    eventAdd("ipcache_purgelru", ipcache_purgelru, NULL, 10.0, 1);
    for (m = ipcache_lru_list.tail; m; m = prev) {
	if (memPoolInUseCount(pool_ipcache) < ipcache_low)
	    break;
	prev = m->prev;
	i = m->data;
	if (i->locks != 0)
	    continue;
	ipcacheRelease(i);
	removed++;
    }
    debugs(14, 9, "ipcache_purgelru: removed %d entries", removed);
}

/* purges entries added from /etc/hosts (or whatever). */
static void
purge_entries_fromhosts(void)
{
    dlink_node *m = ipcache_lru_list.head;
    ipcache_entry *i = NULL, *t;
    while (m) {
	if (i != NULL) {	/* need to delay deletion */
	    ipcacheRelease(i);	/* we just override locks */
	    i = NULL;
	}
	t = m->data;
	if (t->flags.fromhosts)
	    i = t;
	m = m->next;
    }
    if (i != NULL)
	ipcacheRelease(i);
}

/* create blank ipcache_entry */
static ipcache_entry *
ipcacheCreateEntry(const char *name)
{
    static ipcache_entry *i;
    i = memPoolAlloc(pool_ipcache);
    i->hash.key = xstrdup(name);
    i->expires = squid_curtime + namecache_dns_negative_ttl;
    return i;
}

static void
ipcacheAddEntry(ipcache_entry * i)
{
    hash_link *e = hash_lookup(ip_table, i->hash.key);
    if (NULL != e) {
	/* avoid colission */
	ipcache_entry *q = (ipcache_entry *) e;
	ipcacheRelease(q);
    }
    hash_join(ip_table, &i->hash);
    dlinkAdd(i, &i->lru, &ipcache_lru_list);
    i->lastref = squid_curtime;
}

/* walks down the pending list, calling handlers */
static void
ipcacheCallback(ipcache_entry * i)
{
    IPH *handler = i->handler;
    void *handlerData = i->handlerData;
    i->lastref = squid_curtime;
    ipcacheLockEntry(i);
    if (NULL == handler)
	return;
    i->handler = NULL;
    i->handlerData = NULL;
    if (cbdataValid(handlerData)) {
	dns_error_message = i->error_message;
	handler(i->addrs.count ? &i->addrs : NULL, handlerData);
    }
    cbdataUnlock(handlerData);
    ipcacheUnlockEntry(i);
}

static ipcache_entry *
ipcacheParse(ipcache_entry * i, rfc1035_rr * answers, int nr, const char *error_message)
{
    int k;
    int j;
    int na = 0;
    int ttl = 0;
    const char *name = (const char *) i->hash.key;
    i->expires = squid_curtime + namecache_dns_negative_ttl;
    i->flags.negcached = 1;
    safe_free(i->addrs.in_addrs);
    safe_free(i->addrs.bad_mask);
    safe_free(i->error_message);
    i->addrs.count = 0;
    if (nr < 0) {
	debugs(14, 3, "ipcacheParse: Lookup failed '%s' for '%s'",
	    error_message, (const char *) i->hash.key);
	i->error_message = xstrdup(error_message);
	return i;
    }
    if (nr == 0) {
	debugs(14, 3, "ipcacheParse: No DNS records in response to '%s'", name);
	i->error_message = xstrdup("No DNS records");
	return i;
    }
    assert(answers);
    for (k = 0; k < nr; k++) {
	if (answers[k].type != RFC1035_TYPE_A)
	    continue;
	if (answers[k].class != RFC1035_CLASS_IN)
	    continue;
	if (answers[k].rdlength != 4) {
	    debugs(14, 1, "ipcacheParse: Invalid IP address in response to '%s'", name);
	    continue;
	}
	na++;
    }
    if (na == 0) {
	debugs(14, 1, "ipcacheParse: No Address records in response to '%s'", name);
	i->error_message = xstrdup("No Address records");
	return i;
    }
    i->flags.negcached = 0;
    i->addrs.in_addrs = xcalloc(na, sizeof(struct in_addr));
    i->addrs.bad_mask = xcalloc(na, sizeof(unsigned char));
    for (j = 0, k = 0; k < nr; k++) {
	if (answers[k].class != RFC1035_CLASS_IN)
	    continue;
	if (answers[k].type == RFC1035_TYPE_A) {
	    if (answers[k].rdlength != 4)
		continue;
	    xmemcpy(&i->addrs.in_addrs[j++], answers[k].rdata, 4);
	    debugs(14, 3, "ipcacheParse: #%d %s",
		j - 1,
		inet_ntoa(i->addrs.in_addrs[j - 1]));
	} else if (answers[k].type != RFC1035_TYPE_CNAME)
	    continue;
	if (ttl == 0 || ttl > answers[k].ttl)
	    ttl = answers[k].ttl;
    }
    if (na < 256)
	i->addrs.count = (unsigned char) na;
    else
	i->addrs.count = 255;
    if (ttl > namecache_dns_positive_ttl)
	ttl = namecache_dns_positive_ttl;
    if (ttl < namecache_dns_negative_ttl)
	ttl = namecache_dns_negative_ttl;
    i->expires = squid_curtime + ttl;
    assert(j == na);
    return i;
}

static void
ipcacheHandleReply(void *data, rfc1035_rr * answers, int na, const char *error_message)
{
    generic_cbdata *c = data;
    ipcache_entry *i = c->data;
    cbdataFree(c);
    c = NULL;
    IpcacheStats.replies++;
#if NOTYET
    statHistCount(&statCounter.dns.svc_time,
	tvSubMsec(i->request_time, current_time));
#endif
    ipcacheParse(i, answers, na, error_message);
    ipcacheAddEntry(i);
    ipcacheCallback(i);
}

void
ipcache_nbgethostbyname(const char *name, IPH * handler, void *handlerData)
{
    ipcache_entry *i = NULL;
    const ipcache_addrs *addrs = NULL;
    generic_cbdata *c;
    assert(handler != NULL);
    debugs(14, 4, "ipcache_nbgethostbyname: Name '%s'.", name);
    IpcacheStats.requests++;
    if (name == NULL || name[0] == '\0') {
	debugs(14, 4, "ipcache_nbgethostbyname: Invalid name!");
	IpcacheStats.invalid++;
	dns_error_message = "Invalid hostname";
	handler(NULL, handlerData);
	return;
    }
    if ((addrs = ipcacheCheckNumeric(name))) {
	dns_error_message = NULL;
	IpcacheStats.numeric_hits++;
	handler(addrs, handlerData);
	return;
    }
    i = ipcache_get(name);
    if (NULL == i) {
	/* miss */
	(void) 0;
    } else if (ipcacheExpiredEntry(i)) {
	/* hit, but expired -- bummer */
	ipcacheRelease(i);
	i = NULL;
    } else {
	/* hit */
	debugs(14, 4, "ipcache_nbgethostbyname: HIT for '%s'", name);
	if (i->flags.negcached)
	    IpcacheStats.negative_hits++;
	else
	    IpcacheStats.hits++;
	i->handler = handler;
	i->handlerData = handlerData;
	cbdataLock(handlerData);
	ipcacheCallback(i);
	return;
    }
    debugs(14, 5, "ipcache_nbgethostbyname: MISS for '%s'", name);
    IpcacheStats.misses++;
    i = ipcacheCreateEntry(name);
    i->handler = handler;
    i->handlerData = handlerData;
    cbdataLock(handlerData);
    i->request_time = current_time;
    CBDATA_INIT_TYPE(generic_cbdata);
    c = cbdataAlloc(generic_cbdata);
    c->data = i;
    idnsALookup(hashKeyStr(&i->hash), ipcacheHandleReply, c);
}

/* initialize the ipcache */
void
ipcache_init(wordlist *testhosts)
{
    int n;
    debugs(14, 3, "Initializing IP Cache...");
    memset(&IpcacheStats, '\0', sizeof(IpcacheStats));
    memset(&ipcache_lru_list, '\0', sizeof(ipcache_lru_list));
    /* test naming lookup */
    if (namecache_dns_skiptests) {
	debugs(14, 4, "ipcache_init: Skipping DNS name lookup tests.");
    } else if (!ipcache_testname(testhosts)) {
	libcore_fatalf("ipcache_init: DNS name lookup tests failed.", "");
    } else {
	debugs(14, 1, "Successful DNS name lookup tests...");
    }
    memset(&static_addrs, '\0', sizeof(ipcache_addrs));
    static_addrs.in_addrs = xcalloc(1, sizeof(struct in_addr));
    static_addrs.bad_mask = xcalloc(1, sizeof(unsigned char));
    ipcache_high = (long) (((float) namecache_ipcache_size *
	    (float) namecache_ipcache_high) / (float) 100);
    ipcache_low = (long) (((float) namecache_ipcache_size *
	    (float) namecache_ipcache_low) / (float) 100);
    n = hashPrime(ipcache_high / 4);
    ip_table = hash_create((HASHCMP *) strcmp, n, hash4);
    pool_ipcache = memPoolCreate("ipcache_entry", sizeof(ipcache_entry));
}

const ipcache_addrs *
ipcache_gethostbyname(const char *name, int flags)
{
    ipcache_entry *i = NULL;
    ipcache_addrs *addrs;
    assert(name);
    debugs(14, 3, "ipcache_gethostbyname: '%s', flags=%x", name, flags);
    IpcacheStats.requests++;
    i = ipcache_get(name);
    if (NULL == i) {
	(void) 0;
    } else if (ipcacheExpiredEntry(i)) {
	ipcacheRelease(i);
	i = NULL;
    } else if (i->flags.negcached) {
	IpcacheStats.negative_hits++;
	dns_error_message = i->error_message;
	return NULL;
    } else {
	IpcacheStats.hits++;
	i->lastref = squid_curtime;
	dns_error_message = i->error_message;
	return &i->addrs;
    }
    dns_error_message = NULL;
    if ((addrs = ipcacheCheckNumeric(name))) {
	IpcacheStats.numeric_hits++;
	return addrs;
    }
    IpcacheStats.misses++;
    if (flags & IP_LOOKUP_IF_MISS)
	ipcache_nbgethostbyname(name, dummy_handler, NULL);
    return NULL;
}

static void
dummy_handler(const ipcache_addrs * addrsnotused, void *datanotused)
{
    return;
}

void
ipcacheInvalidate(const char *name)
{
    ipcache_entry *i;
    if ((i = ipcache_get(name)) == NULL)
	return;
    i->expires = squid_curtime;
    /*
     * NOTE, don't call ipcacheRelease here becuase we might be here due
     * to a thread started from a callback.
     */
}

void
ipcacheInvalidateNegative(const char *name)
{
    ipcache_entry *i;
    if ((i = ipcache_get(name)) == NULL)
	return;
    if (i->flags.negcached)
	i->expires = squid_curtime;
    /*
     * NOTE, don't call ipcacheRelease here becuase we might be here due
     * to a thread started from a callback.
     */
}

ipcache_addrs *
ipcacheCheckNumeric(const char *name)
{
    struct in_addr ip;
    /* check if it's already a IP address in text form. */
    if (!safe_inet_addr(name, &ip))
	return NULL;
    static_addrs.count = 1;
    static_addrs.cur = 0;
    static_addrs.in_addrs[0].s_addr = ip.s_addr;
    static_addrs.bad_mask[0] = FALSE;
    static_addrs.badcount = 0;
    return &static_addrs;
}

static void
ipcacheLockEntry(ipcache_entry * i)
{
    if (i->locks++ == 0) {
	dlinkDelete(&i->lru, &ipcache_lru_list);
	dlinkAdd(i, &i->lru, &ipcache_lru_list);
    }
}

static void
ipcacheUnlockEntry(ipcache_entry * i)
{
    assert(i->locks > 0);
    i->locks--;
    if (ipcacheExpiredEntry(i))
	ipcacheRelease(i);
}

void
ipcacheCycleAddr(const char *name, ipcache_addrs * ia)
{
    ipcache_entry *i;
    unsigned char k;
    assert(name || ia);
    if (NULL == ia) {
	if ((i = ipcache_get(name)) == NULL)
	    return;
	if (i->flags.negcached)
	    return;
	ia = &i->addrs;
    }
    for (k = 0; k < ia->count; k++) {
	if (++ia->cur == ia->count)
	    ia->cur = 0;
	if (!ia->bad_mask[ia->cur])
	    break;
    }
    if (k == ia->count) {
	/* All bad, reset to All good */
	debugs(14, 3, "ipcacheCycleAddr: Changing ALL %s addrs from BAD to OK",
	    name);
	for (k = 0; k < ia->count; k++)
	    ia->bad_mask[k] = 0;
	ia->badcount = 0;
	ia->cur = 0;
    }
    debugs(14, 3, "ipcacheCycleAddr: %s now at %s", name,
	inet_ntoa(ia->in_addrs[ia->cur]));
}

/*
 * Marks the given address as BAD and calls ipcacheCycleAddr to
 * advance the current pointer to the next OK address.
 */
void
ipcacheMarkBadAddr(const char *name, struct in_addr addr)
{
    ipcache_entry *i;
    ipcache_addrs *ia;
    int k;
    if ((i = ipcache_get(name)) == NULL)
	return;
    ia = &i->addrs;
    for (k = 0; k < (int) ia->count; k++) {
	if (ia->in_addrs[k].s_addr == addr.s_addr)
	    break;
    }
    if (k == (int) ia->count)	/* not found */
	return;
    if (!ia->bad_mask[k]) {
	ia->bad_mask[k] = TRUE;
	ia->badcount++;
	i->expires = XMIN(squid_curtime + XMAX(60, namecache_dns_negative_ttl), i->expires);
	debugs(14, 2, "ipcacheMarkBadAddr: %s [%s]", name, inet_ntoa(addr));
    }
    ipcacheCycleAddr(name, ia);
}

void
ipcacheMarkGoodAddr(const char *name, struct in_addr addr)
{
    ipcache_entry *i;
    ipcache_addrs *ia;
    int k;
    if ((i = ipcache_get(name)) == NULL)
	return;
    ia = &i->addrs;
    for (k = 0; k < (int) ia->count; k++) {
	if (ia->in_addrs[k].s_addr == addr.s_addr)
	    break;
    }
    if (k == (int) ia->count)	/* not found */
	return;
    if (!ia->bad_mask[k])	/* already OK */
	return;
    ia->bad_mask[k] = FALSE;
    ia->badcount--;
    debugs(14, 2, "ipcacheMarkGoodAddr: %s [%s]", name, inet_ntoa(addr));
}

static void
ipcacheFreeEntry(void *data)
{
    ipcache_entry *i = data;
    safe_free(i->addrs.in_addrs);
    safe_free(i->addrs.bad_mask);
    safe_free(i->hash.key);
    safe_free(i->error_message);
    memPoolFree(pool_ipcache, i);
}

void
ipcacheFreeMemory(void)
{
    hashFreeItems(ip_table, ipcacheFreeEntry);
    hashFreeMemory(ip_table);
    ip_table = NULL;
}

/* Recalculate IP cache size upon reconfigure */
void
ipcache_restart(void)
{
    ipcache_high = (long) (((float) namecache_ipcache_size *
	    (float) namecache_ipcache_high) / (float) 100);
    ipcache_low = (long) (((float) namecache_ipcache_size *
	    (float) namecache_ipcache_low) / (float) 100);
    purge_entries_fromhosts();
}

/*
 *  adds a "static" entry from /etc/hosts.  
 *  returns 0 upon success, 1 if the ip address is invalid
 */
int
ipcacheAddEntryFromHosts(const char *name, const char *ipaddr)
{
    ipcache_entry *i;
    struct in_addr ip;
    if (!safe_inet_addr(ipaddr, &ip)) {
	if (strchr(ipaddr, ':') && strspn(ipaddr, "0123456789abcdefABCDEF:") == strlen(ipaddr)) {
	    debugs(14, 3, "ipcacheAddEntryFromHosts: Skipping IPv6 address '%s'", ipaddr);
	} else {
	    debugs(14, 1, "ipcacheAddEntryFromHosts: Bad IP address '%s'",
		ipaddr);
	}
	return 1;
    }
    if ((i = ipcache_get(name))) {
	if (1 == i->flags.fromhosts) {
	    ipcacheUnlockEntry(i);
	} else if (i->locks > 0) {
	    debugs(14, 1, "ipcacheAddEntryFromHosts: can't add static entry"
		" for locked name '%s'\n", name);
	    return 1;
	} else {
	    ipcacheRelease(i);
	}
    }
    i = ipcacheCreateEntry(name);
    i->addrs.count = 1;
    i->addrs.cur = 0;
    i->addrs.badcount = 0;
    i->addrs.in_addrs = xcalloc(1, sizeof(struct in_addr));
    i->addrs.bad_mask = xcalloc(1, sizeof(unsigned char));
    i->addrs.in_addrs[0].s_addr = ip.s_addr;
    i->addrs.bad_mask[0] = FALSE;
    i->flags.fromhosts = 1;
    ipcacheAddEntry(i);
    ipcacheLockEntry(i);
    return 0;
}
int
ipcacheFlushAll(void)
{
    /* code from libsqname/ipcache.c */
    dlink_node *m;
    dlink_node *prev = NULL;
    ipcache_entry *i;
    int removed = 0;

    for (m = ipcache_lru_list.tail; m; m = prev) {
        prev = m->prev;
        i = m->data;
       if (i->flags.fromhosts) /* dont flush entries from /etc/hosts */
           continue;
        ipcacheRelease(i);
        removed++;
    }
    return removed;
}
