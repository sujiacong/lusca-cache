
/*
 * $Id: fqdncache.c 14601 2010-04-16 05:06:55Z adrian.chadd $
 *
 * DEBUG: section 35    FQDN Cache
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
#include <string.h>
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
#include "fqdncache.h"

FqdncacheStatStruct FqdncacheStats;

static dlink_list fqdncache_lru_list;

MemPool * pool_fqdncache = NULL;

static IDNSCB fqdncacheHandleReply;
static fqdncache_entry *fqdncacheParse(fqdncache_entry *, rfc1035_rr *, int, const char *error_message);
static void fqdncacheRelease(fqdncache_entry *);
static fqdncache_entry *fqdncacheCreateEntry(const char *name);
static void fqdncacheCallback(fqdncache_entry *);
static fqdncache_entry *fqdncache_get(const char *);
static FQDNH dummy_handler;
static int fqdncacheExpiredEntry(const fqdncache_entry *);
static void fqdncacheLockEntry(fqdncache_entry * f);
static void fqdncacheUnlockEntry(fqdncache_entry * f);
static FREE fqdncacheFreeEntry;
static void fqdncacheAddEntry(fqdncache_entry * f);

hash_table *fqdn_table = NULL;

static long fqdncache_low = 180;
static long fqdncache_high = 200;

/* removes the given fqdncache entry */
static void
fqdncacheRelease(fqdncache_entry * f)
{
    int k;
    hash_remove_link(fqdn_table, (hash_link *) f);
    for (k = 0; k < (int) f->name_count; k++)
	safe_free(f->names[k]);
    debug(35, 5) ("fqdncacheRelease: Released FQDN record for '%s'.\n",
	hashKeyStr(&f->hash));
    dlinkDelete(&f->lru, &fqdncache_lru_list);
    safe_free(f->hash.key);
    safe_free(f->error_message);
    memPoolFree(pool_fqdncache, f);
}

/* return match for given name */
static fqdncache_entry *
fqdncache_get(const char *name)
{
    hash_link *e;
    static fqdncache_entry *f;
    f = NULL;
    if (fqdn_table) {
	if ((e = hash_lookup(fqdn_table, name)) != NULL)
	    f = (fqdncache_entry *) e;
    }
    return f;
}

static int
fqdncacheExpiredEntry(const fqdncache_entry * f)
{
    /* all static entries are locked, so this takes care of them too */
    if (f->locks != 0)
	return 0;
    if (f->expires > squid_curtime)
	return 0;
    return 1;
}

void
fqdncache_purgelru(void *notused)
{
    dlink_node *m;
    dlink_node *prev = NULL;
    fqdncache_entry *f;
    int removed = 0;
    eventAdd("fqdncache_purgelru", fqdncache_purgelru, NULL, 10.0, 1);
    for (m = fqdncache_lru_list.tail; m; m = prev) {
	if (memPoolInUseCount(pool_fqdncache) < fqdncache_low)
	    break;
	prev = m->prev;
	f = m->data;
	if (f->locks != 0)
	    continue;
	fqdncacheRelease(f);
	removed++;
    }
    debug(35, 9) ("fqdncache_purgelru: removed %d entries\n", removed);
}

static void
purge_entries_fromhosts(void)
{
    dlink_node *m = fqdncache_lru_list.head;
    fqdncache_entry *i = NULL;
    fqdncache_entry *t;
    while (m) {
	if (i != NULL) {	/* need to delay deletion */
	    fqdncacheRelease(i);	/* we just override locks */
	    i = NULL;
	}
	t = m->data;
	if (t->flags.fromhosts)
	    i = t;
	m = m->next;
    }
    if (i != NULL)
	fqdncacheRelease(i);
}

/* create blank fqdncache_entry */
static fqdncache_entry *
fqdncacheCreateEntry(const char *name)
{
    static fqdncache_entry *f;
    f = memPoolAlloc(pool_fqdncache);
    f->hash.key = xstrdup(name);
    f->expires = squid_curtime + namecache_dns_negative_ttl;
    return f;
}

static void
fqdncacheAddEntry(fqdncache_entry * f)
{
    fqdncache_entry *e = (fqdncache_entry *) hash_lookup(fqdn_table, f->hash.key);
    if (NULL != e) {
	/* avoid collision */
	fqdncacheRelease(e);
    }
    hash_join(fqdn_table, &f->hash);
    dlinkAdd(f, &f->lru, &fqdncache_lru_list);
    f->lastref = squid_curtime;
}

/* walks down the pending list, calling handlers */
static void
fqdncacheCallback(fqdncache_entry * f)
{
    FQDNH *handler = f->handler;
    void *handlerData = f->handlerData;
    f->lastref = squid_curtime;
    if (NULL == handler)
	return;
    fqdncacheLockEntry(f);
    f->handler = NULL;
    f->handlerData = NULL;
    if (cbdataValid(handlerData)) {
	dns_error_message = f->error_message;
	handler(f->name_count ? f->names[0] : NULL, handlerData);
    }
    cbdataUnlock(handlerData);
    fqdncacheUnlockEntry(f);
}

static fqdncache_entry *
fqdncacheParse(fqdncache_entry * f, rfc1035_rr * answers, int nr, const char *error_message)
{
    int k;
    int ttl = 0;
    const char *name = (const char *) f->hash.key;
    f->expires = squid_curtime + namecache_dns_negative_ttl;
    f->flags.negcached = 1;
    if (nr < 0) {
	debug(35, 3) ("fqdncacheParse: Lookup of '%s' failed (%s)\n", name, error_message);
	f->error_message = xstrdup(error_message);
	return f;
    }
    if (nr == 0) {
	debug(35, 3) ("fqdncacheParse: No DNS records for '%s'\n", name);
	f->error_message = xstrdup("No DNS records");
	return f;
    }
    debug(35, 3) ("fqdncacheParse: %d answers for '%s'\n", nr, name);
    assert(answers);
    for (k = 0; k < nr; k++) {
	if (answers[k].class != RFC1035_CLASS_IN)
	    continue;
	if (answers[k].type == RFC1035_TYPE_PTR) {
	    if (!answers[k].rdata[0]) {
		debug(35, 2) ("fqdncacheParse: blank PTR record for '%s'\n", name);
		continue;
	    }
	    if (strchr(answers[k].rdata, ' ')) {
		debug(35, 2) ("fqdncacheParse: invalid PTR record '%s' for '%s'\n", answers[k].rdata, name);
		continue;
	    }
	    f->names[f->name_count++] = xstrdup(answers[k].rdata);
	} else if (answers[k].type != RFC1035_TYPE_CNAME)
	    continue;
	if (ttl == 0 || answers[k].ttl < ttl)
	    ttl = answers[k].ttl;
	if (f->name_count >= FQDN_MAX_NAMES)
	    break;
    }
    if (f->name_count == 0) {
	debug(35, 1) ("fqdncacheParse: No PTR record for '%s'\n", name);
	f->error_message = xstrdup("No PTR record");
	return f;
    }
    if (ttl > namecache_dns_positive_ttl)
	ttl = namecache_dns_positive_ttl;
    if (ttl < namecache_dns_negative_ttl)
	ttl = namecache_dns_negative_ttl;
    f->expires = squid_curtime + ttl;
    f->flags.negcached = 0;
    return f;
}

static void
fqdncacheHandleReply(void *data, rfc1035_rr * answers, int na, const char *error_message)
{
    int n;
    generic_cbdata *c = data;
    fqdncache_entry *f = c->data;
    cbdataFree(c);
    c = NULL;
    n = ++FqdncacheStats.replies;
#if NOTYET
    statHistCount(&statCounter.dns.svc_time,
	tvSubMsec(f->request_time, current_time));
#endif
    fqdncacheParse(f, answers, na, error_message);
    fqdncacheAddEntry(f);
    fqdncacheCallback(f);
}

void
fqdncache_nbgethostbyaddr(struct in_addr addr, FQDNH * handler, void *handlerData)
{
    fqdncache_entry *f = NULL;
    char *name = inet_ntoa(addr);
    generic_cbdata *c;
    assert(handler);
    debug(35, 4) ("fqdncache_nbgethostbyaddr: Name '%s'.\n", name);
    FqdncacheStats.requests++;
    if (name == NULL || name[0] == '\0') {
	debug(35, 4) ("fqdncache_nbgethostbyaddr: Invalid name!\n");
	dns_error_message = "Invalid hostname";
	handler(NULL, handlerData);
	return;
    }
    f = fqdncache_get(name);
    if (NULL == f) {
	/* miss */
	(void) 0;
    } else if (fqdncacheExpiredEntry(f)) {
	/* hit, but expired -- bummer */
	fqdncacheRelease(f);
	f = NULL;
    } else {
	/* hit */
	debug(35, 4) ("fqdncache_nbgethostbyaddr: HIT for '%s'\n", name);
	if (f->flags.negcached)
	    FqdncacheStats.negative_hits++;
	else
	    FqdncacheStats.hits++;
	f->handler = handler;
	f->handlerData = handlerData;
	cbdataLock(handlerData);
	fqdncacheCallback(f);
	return;
    }

    debug(35, 5) ("fqdncache_nbgethostbyaddr: MISS for '%s'\n", name);
    FqdncacheStats.misses++;
    f = fqdncacheCreateEntry(name);
    f->handler = handler;
    f->handlerData = handlerData;
    cbdataLock(handlerData);
    f->request_time = current_time;
    CBDATA_INIT_TYPE(generic_cbdata);
    c = cbdataAlloc(generic_cbdata);
    c->data = f;
    idnsPTRLookup(addr, fqdncacheHandleReply, c);
}

/* initialize the fqdncache */
void
fqdncache_init(void)
{
    int n;
    if (fqdn_table)
	return;
    debug(35, 3) ("Initializing FQDN Cache...\n");
    memset(&FqdncacheStats, '\0', sizeof(FqdncacheStats));
    memset(&fqdncache_lru_list, '\0', sizeof(fqdncache_lru_list));
    fqdncache_high = (long) (((float) namecache_fqdncache_size *
	    (float) FQDN_HIGH_WATER) / (float) 100);
    fqdncache_low = (long) (((float) namecache_fqdncache_size *
	    (float) FQDN_LOW_WATER) / (float) 100);
    n = hashPrime(fqdncache_high / 4);
    fqdn_table = hash_create((HASHCMP *) strcmp, n, hash4);
    pool_fqdncache = memPoolCreate("fqdncache_entry", sizeof(fqdncache_entry));
}

const char *
fqdncache_gethostbyaddr(struct in_addr addr, int flags)
{
    char *name = inet_ntoa(addr);
    fqdncache_entry *f = NULL;
    struct in_addr ip;
    assert(name);
    FqdncacheStats.requests++;
    f = fqdncache_get(name);
    if (NULL == f) {
	(void) 0;
    } else if (fqdncacheExpiredEntry(f)) {
	fqdncacheRelease(f);
	f = NULL;
    } else if (f->flags.negcached) {
	FqdncacheStats.negative_hits++;
	dns_error_message = f->error_message;
	return NULL;
    } else {
	FqdncacheStats.hits++;
	f->lastref = squid_curtime;
	dns_error_message = f->error_message;
	return f->names[0];
    }
    dns_error_message = NULL;
    /* check if it's already a FQDN address in text form. */
    if (!safe_inet_addr(name, &ip))
	return name;
    FqdncacheStats.misses++;
    if (flags & FQDN_LOOKUP_IF_MISS)
	fqdncache_nbgethostbyaddr(addr, dummy_handler, NULL);
    return NULL;
}


static void
dummy_handler(const char *bufnotused, void *datanotused)
{
    return;
}

const char *
fqdnFromAddr(struct in_addr addr)
{
    const char *n;
    static char buf[32];
    if (namecache_fqdncache_logfqdn && (n = fqdncache_gethostbyaddr(addr, 0)))
	return n;
    xstrncpy(buf, inet_ntoa(addr), 32);
    return buf;
}

static void
fqdncacheLockEntry(fqdncache_entry * f)
{
    if (f->locks++ == 0) {
	dlinkDelete(&f->lru, &fqdncache_lru_list);
	dlinkAdd(f, &f->lru, &fqdncache_lru_list);
    }
}

static void
fqdncacheUnlockEntry(fqdncache_entry * f)
{
    assert(f->locks > 0);
    f->locks--;
    if (fqdncacheExpiredEntry(f))
	fqdncacheRelease(f);
}

static void
fqdncacheFreeEntry(void *data)
{
    fqdncache_entry *f = data;
    int k;
    for (k = 0; k < (int) f->name_count; k++)
	safe_free(f->names[k]);
    safe_free(f->hash.key);
    safe_free(f->error_message);
    memPoolFree(pool_fqdncache, f);
}

void
fqdncacheFreeMemory(void)
{
    hashFreeItems(fqdn_table, fqdncacheFreeEntry);
    hashFreeMemory(fqdn_table);
    fqdn_table = NULL;
}

/* Recalculate FQDN cache size upon reconfigure */
void
fqdncache_restart(void)
{
    fqdncache_high = (long) (((float) namecache_fqdncache_size *
	    (float) FQDN_HIGH_WATER) / (float) 100);
    fqdncache_low = (long) (((float) namecache_fqdncache_size *
	    (float) FQDN_LOW_WATER) / (float) 100);
    purge_entries_fromhosts();
}

/*
 *  adds a "static" entry from /etc/hosts.  the worldist is to be
 *  managed by the caller, including pointed-to strings
 */
void
fqdncacheAddEntryFromHosts(char *addr, wordlist * hostnames)
{
    fqdncache_entry *fce;
    int j = 0;
    if ((fce = fqdncache_get(addr))) {
	if (1 == fce->flags.fromhosts) {
	    fqdncacheUnlockEntry(fce);
	} else if (fce->locks > 0) {
	    debug(35, 1) ("fqdncacheAddEntryFromHosts: can't add static entry for locked address '%s'\n", addr);
	    return;
	} else {
	    fqdncacheRelease(fce);
	}
    }
    fce = fqdncacheCreateEntry(addr);
    while (hostnames) {
	fce->names[j] = xstrdup(hostnames->key);
	j++;
	hostnames = hostnames->next;
	if (j >= FQDN_MAX_NAMES)
	    break;
    }
    fce->name_count = j;
    fce->names[j] = NULL;	/* it's safe */
    fce->flags.fromhosts = 1;
    fqdncacheAddEntry(fce);
    fqdncacheLockEntry(fce);
}
int
fqdncacheFlushAll(void)
{
    /* code from libsqname/fqdncache.c */
    dlink_node *m;
    dlink_node *prev = NULL;
    fqdncache_entry *f;
    int removed = 0;

    for (m = fqdncache_lru_list.tail; m; m = prev) {
        prev = m->prev;
        f = m->data;
        if (f->flags.fromhosts) /* dont flush entries from /etc/hosts */
            continue;
        fqdncacheRelease(f);
        removed++;
    }
    return removed;
}
