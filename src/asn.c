
/*
 * $Id: asn.c 14802 2010-09-08 05:00:25Z adrian.chadd $
 *
 * DEBUG: section 53    AS Number handling
 * AUTHOR: Duane Wessels, Kostas Anagnostakis
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
#include "../libcore/radix.h"

#define WHOIS_PORT 43

radix_tree_t * AS_tree = NULL;

/*
 * Structure for as number information. it could be simply 
 * an intlist but it's coded as a structure for future
 * enhancements (e.g. expires)
 */
struct _as_info {
    intlist *as_number;
    time_t expires;		/* NOTUSED */
};

struct _ASState {
    StoreEntry *entry;
    store_client *sc;
    request_t *request;
    int as_number;
    squid_off_t seen;
    squid_off_t offset;
};

typedef struct _ASState ASState;
typedef struct _as_info as_info;

/* entry into the radix tree */
struct _rtentry {
    as_info *e_info;
};

typedef struct _rtentry rtentry;

static int asnAddNet(char *, int);
static void asnCacheStart(int as);
static STNCB asHandleReply;
static int printRadixNode(StoreEntry *e, radix_node_t *n);
static void asnAclInitialize(acl * acls);
static void asStateFree(void *data);
static void destroyRadixNodeInfo(as_info *);
static OBJH asnStats;

/* PUBLIC */


int
asnMatchIp(void *data, sqaddr_t *v6addr)
{
	struct in_addr a;

//#warning ASN code needs to be made ipv6 aware

    if (sqinet_get_family(v6addr) != AF_INET)
        return 0;

    a = sqinet_get_v4_inaddr(v6addr, SQADDR_ASSERT_IS_V4);
    return asnMatchIp4(data, a);
}

int
asnMatchIp4(void *data, struct in_addr addr)
{
    as_info *e;
    intlist *a = NULL;
    intlist *b = NULL;
    prefix_t *p;
    radix_node_t *n;

    debugs(53, 3, "asnMatchIp: Called for %s.", inet_ntoa(addr));

    if (AS_tree == NULL)
	return 0;
    if (IsNoAddr(&addr))
	return 0;
    if (IsAnyAddr(&addr))
	return 0;

    p = New_Prefix(AF_INET, &addr, 32, NULL);
    n = radix_search_best(AS_tree, p);
    Deref_Prefix(p);
    if (n == NULL) {
	debugs(53, 3, "asnMatchIp: Address not in as db.");
	return 0;
    }

    debugs(53, 3, "asnMatchIp: Found in db!");
    e = ((rtentry *) (n->data))->e_info;
    assert(e);
    for (a = (intlist *) data; a; a = a->next)
	for (b = e->as_number; b; b = b->next)
	    if (a->i == b->i) {
		debugs(53, 5, "asnMatchIp: Found a match!");
		return 1;
	    }
    debugs(53, 5, "asnMatchIp: AS not in as db.");
    return 0;
}

static void
asnAclInitialize(acl * acls)
{
    acl *a;
    intlist *i;
    debugs(53, 3, "asnAclInitialize");
    for (a = acls; a; a = a->next) {
	if (a->type != ACL_DST_ASN && a->type != ACL_SRC_ASN)
	    continue;
	for (i = a->data; i; i = i->next)
	    asnCacheStart(i->i);
    }
}

CBDATA_TYPE(ASState);
void
asnInit(void)
{
    CBDATA_INIT_TYPE(ASState);
    AS_tree = New_Radix();
    asnAclInitialize(Config.aclList);
    cachemgrRegister("asndb", "AS Number Database", asnStats, NULL, NULL, 0, 1, 0);
}

static void
asnFreeRadixNode(radix_node_t *ptr, void *cbdata)
{
	rtentry *rt = ptr->data;

	/* free the node list */
        destroyRadixNodeInfo(rt->e_info);

	/* free the node */
	xfree(rt);
	ptr->data = NULL;
}

void
asnFreeMemory(void)
{
    Destroy_Radix(AS_tree, asnFreeRadixNode, NULL);
    AS_tree = NULL;
}

static void
asnStats(StoreEntry * sentry, void* data)
{
    radix_node_t *n;

    storeAppendPrintf(sentry, "Address    \tAS Numbers\n");

    RADIX_WALK(AS_tree->head, n) {
        printRadixNode(sentry, n);
    } RADIX_WALK_END;
}

/* PRIVATE */


static void
asnCacheStart(int as)
{
    LOCAL_ARRAY(char, asres, 4096);
    StoreEntry *e;
    request_t *req;
    ASState *asState;
    method_t *method_get;
    method_get = urlMethodGetKnownByCode(METHOD_GET);
    asState = cbdataAlloc(ASState);
    debugs(53, 3, "asnCacheStart: AS %d", as);
    snprintf(asres, 4096, "whois://%s/!gAS%d", Config.as_whois_server, as);
    asState->as_number = as;
    req = urlParse(method_get, asres);
    assert(NULL != req);
    asState->request = requestLink(req);
    if ((e = storeGetPublic(asres, method_get)) == NULL) {
	e = storeCreateEntry(asres, null_request_flags, method_get);
	asState->sc = storeClientRegister(e, asState);
	fwdStart(-1, e, asState->request);
    } else {
	storeLockObject(e);
	asState->sc = storeClientRegister(e, asState);
    }
    asState->entry = e;
    asState->seen = 0;
    asState->offset = 0;
    storeClientRef(asState->sc,
	e,
	asState->seen,
	asState->offset,
	SM_PAGE_SIZE,
	asHandleReply,
	asState);
}

static void
asHandleReply(void *data, mem_node_ref nr, ssize_t size)
{
    ASState *asState = data;
    StoreEntry *e = asState->entry;
    char *s;
    char *t;
    LOCAL_ARRAY(char, buf, SM_PAGE_SIZE);

    debugs(53, 3, "asHandleReply: Called with size=%ld", (long int) size);
    if (EBIT_TEST(e->flags, ENTRY_ABORTED)) {
	stmemNodeUnref(&nr);
	asStateFree(asState);
	return;
    }
    if (size == 0 && e->mem_obj->inmem_hi > 0) {
	asStateFree(asState);
	stmemNodeUnref(&nr);
	return;
    } else if (size < 0) {
	debugs(53, 1, "asHandleReply: Called with size=%ld", (long int) size);
	asStateFree(asState);
	stmemNodeUnref(&nr);
	return;
    } else if (HTTP_OK != e->mem_obj->reply->sline.status) {
	debugs(53, 1, "WARNING: AS %d whois request failed",
	    asState->as_number);
	stmemNodeUnref(&nr);
	asStateFree(asState);
	return;
    }
    assert((nr.offset + size) <= SM_PAGE_SIZE);
    memcpy(buf, nr.node->data + nr.offset, size);
    stmemNodeUnref(&nr);

    s = buf;
    while (s - buf < size && *s != '\0') {
	while (*s && xisspace(*s))
	    s++;
	for (t = s; *t; t++) {
	    if (xisspace(*t))
		break;
	}
	if (*t == '\0') {
	    /* oof, word should continue on next block */
	    break;
	}
	*t = '\0';
	debugs(53, 3, "asHandleReply: AS# %s (%d)", s, asState->as_number);
	asnAddNet(s, asState->as_number);
	s = t + 1;
    }
    asState->seen = asState->offset + size;
    asState->offset += (s - buf);
    debugs(53, 3, "asState->seen = %ld, asState->offset = %ld",
	(long int) asState->seen, (long int) asState->offset);
    if (e->store_status == STORE_PENDING) {
	debugs(53, 3, "asHandleReply: store_status == STORE_PENDING: %s", storeUrl(e));
	storeClientRef(asState->sc,
	    e,
	    asState->seen,
	    asState->offset,
	    SM_PAGE_SIZE,
	    asHandleReply,
	    asState);
    } else if (asState->seen < e->mem_obj->inmem_hi) {
	debugs(53, 3, "asHandleReply: asState->seen < e->mem_obj->inmem_hi %s", storeUrl(e));
	storeClientRef(asState->sc,
	    e,
	    asState->seen,
	    asState->offset,
	    SM_PAGE_SIZE,
	    asHandleReply,
	    asState);
    } else {
	debugs(53, 3, "asHandleReply: Done: %s", storeUrl(e));
	asStateFree(asState);
    }
}

static void
asStateFree(void *data)
{
    ASState *asState = data;
    debugs(53, 3, "asStateFree: %s", storeUrl(asState->entry));
    storeClientUnregister(asState->sc, asState->entry, asState);
    storeUnlockObject(asState->entry);
    requestUnlink(asState->request);
    cbdataFree(asState);
}


/* add a network (addr, mask) to the radix tree, with matching AS
 * number */

static int
asnAddNet(char *as_string, int as_number)
{
    rtentry *e;
    prefix_t *p;
    radix_node_t *n;
    char dbg1[32], dbg2[32];
    intlist **Tail = NULL;
    intlist *q = NULL;
    as_info *asinfo = NULL;
    char *t;
    int bitl;
    in_addr_t mask;
    struct in_addr in_a, in_m;

    t = strchr(as_string, '/');
    if (t == NULL) {
	debugs(53, 3, "asnAddNet: failed, invalid response from whois server.");
	return 0;
    }
    *t = '\0';
    in_a.s_addr = inet_addr(as_string);
    bitl = atoi(t + 1);
    if (bitl < 0)
	bitl = 0;
    if (bitl > 32)
	bitl = 32;
    mask = bitl ? 0xfffffffful << (32 - bitl) : 0;

    in_m.s_addr = mask;
    xstrncpy(dbg1, inet_ntoa(in_a), 32);
    xstrncpy(dbg2, inet_ntoa(in_m), 32);
    debugs(53, 3, "asnAddNet: called for %s/%s", dbg1, dbg2);

    e = xcalloc(1, sizeof(rtentry));

    p = New_Prefix(AF_INET, &in_a, bitl, NULL);
    n = radix_search_exact(AS_tree, p);

    /* Does the entry exist? Append the given ASN to it */
    if (n != NULL) {
	asinfo = ((rtentry *) (n->data))->e_info;
	if (intlistFind(asinfo->as_number, as_number)) {
	    debugs(53, 3, "asnAddNet: Ignoring repeated network '%s/%d' for AS %d",
		dbg1, bitl, as_number);
	} else {
	    debugs(53, 3, "asnAddNet: Warning: Found a network with multiple AS numbers!");
	    for (Tail = &asinfo->as_number; *Tail; Tail = &(*Tail)->next);
	    q = xcalloc(1, sizeof(intlist));
	    q->i = as_number;
	    *(Tail) = q;
	    e->e_info = asinfo;
	}
    } else {
	q = xcalloc(1, sizeof(intlist));
	q->i = as_number;
	asinfo = xmalloc(sizeof(asinfo));
	asinfo->as_number = q;
        n = radix_lookup(AS_tree, p);
        assert(n != NULL);
	e->e_info = asinfo;
        n->data = e;
    }
    e->e_info = asinfo;
    Deref_Prefix(p);
    return 1;
}

static void
destroyRadixNodeInfo(as_info * e_info)
{
    intlist *prev = NULL;
    intlist *data = e_info->as_number;
    while (data) {
	prev = data;
	data = data->next;
	xfree(prev);
    }
    xfree(data);
}

static int
mask_len(u_long mask)
{
    int len = 32;
    if (mask == 0)
	return 0;
    while ((mask & 1) == 0) {
	len--;
	mask >>= 1;
    }
    return len;
}

static int
printRadixNode(StoreEntry *sentry, radix_node_t *n)
{
    rtentry *e = n->data;
    intlist *q;
    as_info *asinfo;
    char buf[128];

    assert(e);
    assert(e->e_info);

    (void) prefix_ntop(n->prefix, buf, sizeof(buf)-1);

    storeAppendPrintf(sentry, "%s\t", buf);
    asinfo = e->e_info;
    assert(asinfo->as_number);
    for (q = asinfo->as_number; q; q = q->next)
	storeAppendPrintf(sentry, " %d", q->i);
    storeAppendPrintf(sentry, "\n");
    return 0;
}
