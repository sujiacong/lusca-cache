
/*
 * $Id: client_db.c 14623 2010-04-19 09:27:56Z adrian.chadd $
 *
 * DEBUG: section 0     Client Database
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

#include "../libcore/radix.h"

#include "client_db.h"

#define	CLIENT_DB_SCHEDULE_BACKGROUND_TIME	300
#define	CLIENT_DB_SCHEDULE_IMMEDIATE_TIME	5

struct _ClientInfo {
    struct in_addr addr;
    dlink_node node;
    struct {
        int result_hist[LOG_TYPE_MAX];
        int n_requests;
        kb_t kbytes_in;
        kb_t kbytes_out;
        kb_t hit_kbytes_out;
    } Http, Icp;
    struct {
        time_t time;
        int n_req;
        int n_denied;
    } cutoff;
    int n_established;          /* number of current established connections */
    time_t last_seen;
};

typedef struct _ClientInfo ClientInfo;

static radix_tree_t *client_v4_tree = NULL;
dlink_list client_list;

static ClientInfo *clientdbAdd(struct in_addr addr);
static void clientdbStartGC(void);
static void clientdbScheduledGC(void *);

static int max_clients = 32;
static int cleanup_running = 0;
static int cleanup_scheduled = 0;
static int cleanup_removed;

static MemPool * pool_client_info;

static ClientInfo *
clientdbAdd(struct in_addr addr)
{
    radix_node_t *rn;
    prefix_t p;
    ClientInfo *c;

    Init_Prefix(&p, AF_INET, &addr, 32);
    c = memPoolAlloc(pool_client_info);
    c->addr = addr;
    rn = radix_lookup(client_v4_tree, &p);
    rn->data = c;
    dlinkAddTail(c, &c->node, &client_list);
    statCounter.client_http.clients++;
    if ((statCounter.client_http.clients > max_clients) && !cleanup_running && !cleanup_scheduled) {
	cleanup_scheduled = 1;
	eventAdd("client_db garbage collector", clientdbScheduledGC, NULL, CLIENT_DB_SCHEDULE_IMMEDIATE_TIME, 0);
    }
    return c;
}

void
clientdbInitMem(void)
{
    pool_client_info = memPoolCreate("ClientInfo", sizeof(ClientInfo));
    eventAdd("client_db garbage collector", clientdbScheduledGC, NULL, CLIENT_DB_SCHEDULE_BACKGROUND_TIME, 0);
}

void
clientdbInit(void)
{
    if (client_v4_tree)
        return;
    client_v4_tree = New_Radix();
    cachemgrRegister("client_list", "Cache Client List", clientdbDump, 0, 1);
}

void
clientdbUpdate(struct in_addr addr, log_type ltype, protocol_t p, squid_off_t size)
{
    radix_node_t *rn;
    prefix_t pr;
    ClientInfo *c = NULL;

    if (!Config.onoff.client_db)
	return;

    Init_Prefix(&pr, AF_INET, &addr, 32);
    rn = radix_search_exact(client_v4_tree, &pr);

    if (rn)
        c = rn->data;
    if (c == NULL)
	c = clientdbAdd(addr);
    if (c == NULL)
	debug_trap("clientdbUpdate: Failed to add entry");
    if (p == PROTO_HTTP) {
	c->Http.n_requests++;
	c->Http.result_hist[ltype]++;
	kb_incr(&c->Http.kbytes_out, size);
	if (isTcpHit(ltype))
	    kb_incr(&c->Http.hit_kbytes_out, size);
    } else if (p == PROTO_ICP) {
	c->Icp.n_requests++;
	c->Icp.result_hist[ltype]++;
	kb_incr(&c->Icp.kbytes_out, size);
	if (LOG_UDP_HIT == ltype)
	    kb_incr(&c->Icp.hit_kbytes_out, size);
    }
    c->last_seen = squid_curtime;
}

/*
 * clientdbEstablished()
 * This function tracks the number of currently established connections
 * for a client IP address.  When a connection is accepted, call this
 * with delta = 1.  When the connection is closed, call with delta =
 * -1.  To get the current value, simply call with delta = 0.
 */
int
clientdbEstablished(struct in_addr addr, int delta)
{
    ClientInfo *c = NULL;
    prefix_t p;
    radix_node_t *rn;

    if (!Config.onoff.client_db)
	return 0;
    Init_Prefix(&p, AF_INET, &addr, 32);
    rn = radix_search_exact(client_v4_tree, &p);
    if (rn)
        c = rn->data;
    if (c == NULL)
	c = clientdbAdd(addr);
    if (c == NULL)
	debug_trap("clientdbUpdate: Failed to add entry");
    c->n_established += delta;
    return c->n_established;
}

#define CUTOFF_SECONDS 3600
int
clientdbCutoffDenied(struct in_addr addr)
{
    int NR;
    int ND;
    double p;
    ClientInfo *c = NULL;
    prefix_t pr;
    radix_node_t *rn;

    if (!Config.onoff.client_db)
	return 0;

    Init_Prefix(&pr, AF_INET, &addr, 32);
    rn = radix_search_exact(client_v4_tree, &pr);
    if (rn)
        c = rn->data;

    if (c == NULL)
	return 0;
    /*
     * If we are in a cutoff window, we don't send a reply
     */
    if (squid_curtime - c->cutoff.time < CUTOFF_SECONDS)
	return 1;
    /*
     * Calculate the percent of DENIED replies since the last
     * cutoff time.
     */
    NR = c->Icp.n_requests - c->cutoff.n_req;
    if (NR < 150)
	NR = 150;
    ND = c->Icp.result_hist[LOG_UDP_DENIED] - c->cutoff.n_denied;
    p = 100.0 * ND / NR;
    if (p < 95.0)
	return 0;
    debug(1, 0) ("WARNING: Probable misconfigured neighbor at %s\n", inet_ntoa(addr));
    debug(1, 0) ("WARNING: %d of the last %d ICP replies are DENIED\n", ND, NR);
    debug(1, 0) ("WARNING: No replies will be sent for the next %d seconds\n",
	CUTOFF_SECONDS);
    c->cutoff.time = squid_curtime;
    c->cutoff.n_req = c->Icp.n_requests;
    c->cutoff.n_denied = c->Icp.result_hist[LOG_UDP_DENIED];
    return 1;
}

struct
clientdb_iterate_stats {
	int http_total;
	int icp_total;
	int http_hits;
	int icp_hits;
};

static void
clientdbDumpEntry(StoreEntry *sentry, ClientInfo *c, struct clientdb_iterate_stats *ci)
{
        log_type l;

	storeAppendPrintf(sentry, "Address: %s\n", xinet_ntoa(c->addr));
	storeAppendPrintf(sentry, "Name: %s\n", fqdnFromAddr(c->addr));
	storeAppendPrintf(sentry, "Currently established connections: %d\n",
	    c->n_established);
	storeAppendPrintf(sentry, "    ICP Requests %d\n",
	    c->Icp.n_requests);
	for (l = LOG_TAG_NONE; l < LOG_TYPE_MAX; l++) {
	    if (c->Icp.result_hist[l] == 0)
		continue;
	    ci->icp_total += c->Icp.result_hist[l];
	    if (LOG_UDP_HIT == l)
		ci->icp_hits += c->Icp.result_hist[l];
	    storeAppendPrintf(sentry,
		"        %-20.20s %7d %3d%%\n",
		log_tags[l],
		c->Icp.result_hist[l],
		percent(c->Icp.result_hist[l], c->Icp.n_requests));
	}
	storeAppendPrintf(sentry, "    HTTP Requests %d\n",
	    c->Http.n_requests);
	for (l = LOG_TAG_NONE; l < LOG_TYPE_MAX; l++) {
	    if (c->Http.result_hist[l] == 0)
		continue;
	    ci->http_total += c->Http.result_hist[l];
	    if (isTcpHit(l))
		ci->http_hits += c->Http.result_hist[l];
	    storeAppendPrintf(sentry,
		"        %-20.20s %7d %3d%%\n",
		log_tags[l],
		c->Http.result_hist[l],
		percent(c->Http.result_hist[l], c->Http.n_requests));
	}
	storeAppendPrintf(sentry, "\n");
}

void
clientdbDump(StoreEntry * sentry)
{
    ClientInfo *c;
    radix_node_t *rn;
    struct clientdb_iterate_stats ci;

    memset(&ci, 0, sizeof(ci));
    storeAppendPrintf(sentry, "Cache Clients:\n");

    RADIX_WALK(client_v4_tree->head, rn) {
      c = rn->data;
      clientdbDumpEntry(sentry, c, &ci);
    } RADIX_WALK_END;

    storeAppendPrintf(sentry, "TOTALS\n");
    storeAppendPrintf(sentry, "ICP : %d Queries, %d Hits (%3d%%)\n",
	ci.icp_total, ci.icp_hits, percent(ci.icp_hits, ci.icp_total));
    storeAppendPrintf(sentry, "HTTP: %d Requests, %d Hits (%3d%%)\n",
	ci.http_total, ci.http_hits, percent(ci.http_hits, ci.http_total));
}

static void
clientdbFreeItem(ClientInfo *c)
{
    dlinkDelete(&c->node, &client_list);
    memPoolFree(pool_client_info, c);
}

static void
clientdbFreeItemRadix(radix_node_t *rn, void *cbdata)
{
	ClientInfo *c = rn->data;

	rn->data = NULL;
	clientdbFreeItem(c);
}

void
clientdbFreeMemory(void)
{
    Destroy_Radix(client_v4_tree, clientdbFreeItemRadix, NULL);
    client_v4_tree = NULL;
}

static void
clientdbScheduledGC(void *unused)
{
    cleanup_scheduled = 0;
    clientdbStartGC();
}

static void
clientdbGC(void *unused)   
{
    radix_node_t *rn;
    dlink_node *n = client_list.head;
    prefix_t p;

    while (n != NULL) {
      ClientInfo *c = n->data;
      n = n->next;
      int age = squid_curtime - c->last_seen;
      if (c->n_established)
          continue;
      if (age < 24 * 3600 && c->Http.n_requests > 100)
          continue;
      if (age < 4 * 3600 && (c->Http.n_requests > 10 || c->Icp.n_requests > 10))
          continue;
      if (age < 5 * 60 && (c->Http.n_requests > 1 || c->Icp.n_requests > 1))
          continue;
      if (age < 60)
          continue;

      Init_Prefix(&p, AF_INET, &c->addr, 32);
      rn = radix_search_exact(client_v4_tree, &p);
      rn->data = NULL;
      radix_remove(client_v4_tree, rn);
      clientdbFreeItem(c);

      cleanup_removed++;
      statCounter.client_http.clients--;
    }

    if (!cleanup_scheduled) {
        cleanup_scheduled = 1;
        eventAdd("client_db garbage collector", clientdbScheduledGC, NULL, CLIENT_DB_SCHEDULE_IMMEDIATE_TIME, 0);
    }

    debug(49, 2) ("clientdbGC: Removed %d entries\n", cleanup_removed);
}

static void
clientdbStartGC(void)
{
    max_clients = statCounter.client_http.clients;
    cleanup_running = 1;
    cleanup_removed = 0;
    clientdbGC(NULL);
}

/*
 * XXX this has been disabled due to changes to the client db code.
 * XXX The client db code used to use an IPv4 hash table; it will
 * XXX use an IPv4/IPv6 radix tree. The SNMP code should be
 * XXX modified to use the radix tree and be "IPv6 compatible"
 * XXX in whatever way Squid-3's client database is.
 */
#if SQUID_SNMP
struct in_addr *
client_entry(struct in_addr *current)
{
#if 0
    ClientInfo *c = NULL;
    const char *key;
    if (current) {
	key = xinet_ntoa(*current);
	hash_first(client_table);
	while ((c = (ClientInfo *) hash_next(client_table))) {
	    if (!strcmp(key, hashKeyStr(&c->hash)))
		break;
	}
	c = (ClientInfo *) hash_next(client_table);
    } else {
	hash_first(client_table);
	c = (ClientInfo *) hash_next(client_table);
    }
    hash_last(client_table);
    if (c)
	return (&c->addr);
    else
#endif
	return (NULL);

}

variable_list *
snmp_meshCtblFn(variable_list * Var, snint * ErrP)
{
    variable_list *Answer = NULL;
#if 0
    static char key[16];
    ClientInfo *c = NULL;
    int aggr = 0;
    log_type l;
#endif
    *ErrP = SNMP_ERR_NOERROR;
#if 0
    debug(49, 6) ("snmp_meshCtblFn: Current : \n");
    snmpDebugOid(6, Var->name, Var->name_length);
    snprintf(key, sizeof(key), "%d.%d.%d.%d", Var->name[LEN_SQ_NET + 3], Var->name[LEN_SQ_NET + 4],
	Var->name[LEN_SQ_NET + 5], Var->name[LEN_SQ_NET + 6]);
    debug(49, 5) ("snmp_meshCtblFn: [%s] requested!\n", key);
    c = (ClientInfo *) hash_lookup(client_table, key);
    if (c == NULL) {
	debug(49, 5) ("snmp_meshCtblFn: not found.\n");
	*ErrP = SNMP_ERR_NOSUCHNAME;
	return NULL;
    }
    switch (Var->name[LEN_SQ_NET + 2]) {
    case MESH_CTBL_ADDR:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    (snint) c->addr.s_addr,
	    SMI_IPADDRESS);
	break;
    case MESH_CTBL_HTBYTES:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    (snint) c->Http.kbytes_out.kb,
	    SMI_COUNTER32);
	break;
    case MESH_CTBL_HTREQ:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    (snint) c->Http.n_requests,
	    SMI_COUNTER32);
	break;
    case MESH_CTBL_HTHITS:
	aggr = 0;
	for (l = LOG_TAG_NONE; l < LOG_TYPE_MAX; l++) {
	    if (isTcpHit(l))
		aggr += c->Http.result_hist[l];
	}
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    (snint) aggr,
	    SMI_COUNTER32);
	break;
    case MESH_CTBL_HTHITBYTES:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    (snint) c->Http.hit_kbytes_out.kb,
	    SMI_COUNTER32);
	break;
    case MESH_CTBL_ICPBYTES:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    (snint) c->Icp.kbytes_out.kb,
	    SMI_COUNTER32);
	break;
    case MESH_CTBL_ICPREQ:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    (snint) c->Icp.n_requests,
	    SMI_COUNTER32);
	break;
    case MESH_CTBL_ICPHITS:
	aggr = c->Icp.result_hist[LOG_UDP_HIT];
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    (snint) aggr,
	    SMI_COUNTER32);
	break;
    case MESH_CTBL_ICPHITBYTES:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    (snint) c->Icp.hit_kbytes_out.kb,
	    SMI_COUNTER32);
	break;
    default:
	*ErrP = SNMP_ERR_NOSUCHNAME;
	debug(49, 5) ("snmp_meshCtblFn: illegal column.\n");
	break;
    }
#endif
    return Answer;
}

#endif /*SQUID_SNMP */
