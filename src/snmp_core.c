
/*
 * $Id: snmp_core.c 14592 2010-04-12 02:49:38Z adrian.chadd $
 *
 * DEBUG: section 49    SNMP support
 * AUTHOR: Glenn Chisholm
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
#include "cache_snmp.h"

#include "../include/strsep.h"

#define SNMP_REQUEST_SIZE 4096
#define MAX_PROTOSTAT 5

typedef struct _mib_tree_entry mib_tree_entry;
typedef oid *(instance_Fn) (oid * name, snint * len, mib_tree_entry * current, oid_ParseFn ** Fn);

struct _mib_tree_entry {
    oid *name;
    int len;
    oid_ParseFn *parsefunction;
    instance_Fn *instancefunction;
    int children;
    struct _mib_tree_entry **leaves;
    struct _mib_tree_entry *parent;
};

mib_tree_entry *mib_tree_head;
mib_tree_entry *mib_tree_last;

static mib_tree_entry * snmpAddNodeStr(const char *base_str, int o, oid_ParseFn * parsefunction, instance_Fn * instancefunction);
#if STDC_HEADERS
static mib_tree_entry *snmpAddNode(oid * name, int len, oid_ParseFn * parsefunction, instance_Fn * instancefunction, int children,...);
static oid *snmpCreateOid(int length,...);
#else
static mib_tree_entry *snmpAddNode();
static oid *snmpCreateOid();
#endif
mib_tree_entry * snmpLookupNodeStr(mib_tree_entry *entry, const char *str);
int snmpCreateOidFromStr(const char *str, oid **name, int *nl);
extern void (*snmplib_debug_hook) (int, char *);
static oid *static_Inst(oid * name, snint * len, mib_tree_entry * current, oid_ParseFn ** Fn);
static oid *time_Inst(oid * name, snint * len, mib_tree_entry * current, oid_ParseFn ** Fn);
static oid *peer_Inst(oid * name, snint * len, mib_tree_entry * current, oid_ParseFn ** Fn);
static oid *peer_InstIndex(oid * name, snint * len, mib_tree_entry * current, oid_ParseFn ** Fn);
#if 0
static oid *client_Inst(oid * name, snint * len, mib_tree_entry * current, oid_ParseFn ** Fn);
#endif
static void snmpDecodePacket(snmp_request_t * rq);
static void snmpConstructReponse(snmp_request_t * rq);
static struct snmp_pdu *snmpAgentResponse(struct snmp_pdu *PDU);
static oid_ParseFn *snmpTreeNext(oid * Current, snint CurrentLen, oid ** Next, snint * NextLen);
static oid_ParseFn *snmpTreeGet(oid * Current, snint CurrentLen);
static mib_tree_entry *snmpTreeEntry(oid entry, snint len, mib_tree_entry * current);
static mib_tree_entry *snmpTreeSiblingEntry(oid entry, snint len, mib_tree_entry * current);
static void snmpSnmplibDebug(int lvl, char *buf);


/*
 * The functions used during startup:
 * snmpInit
 * snmpConnectionOpen
 * snmpConnectionShutdown
 * snmpConnectionClose
 */

/*
 * Turns the MIB into a Tree structure. Called during the startup process.
 */
void
snmpInit(void)
{
    mib_tree_entry * n, *m2;

    debug(49, 5) ("snmpInit: Called.\n");

    debug(49, 5) ("snmpInit: Building SNMP mib tree structure\n");

    snmplib_debug_hook = snmpSnmplibDebug;

	/* 
	 * This following bit of evil is to get the final node in the "squid" mib
	 * without having a "search" function. A search function should be written
	 * to make this and the other code much less evil.
	 */
	mib_tree_head = snmpAddNode(snmpCreateOid(1, 1), 1, NULL, NULL, 0);
	assert(mib_tree_head);
	debug(49, 5) ("snmpInit: root is %p\n", mib_tree_head);
	snmpAddNodeStr("1", 3, NULL, NULL);

	snmpAddNodeStr("1.3", 6, NULL, NULL);
	snmpAddNodeStr("1.3.6", 1, NULL, NULL);
	snmpAddNodeStr("1.3.6.1", 4, NULL, NULL);
	snmpAddNodeStr("1.3.6.1.4", 1, NULL, NULL);
	snmpAddNodeStr("1.3.6.1.4.1", 3495, NULL, NULL);
	m2 = snmpAddNodeStr("1.3.6.1.4.1.3495", 1, NULL, NULL);

	n = snmpLookupNodeStr(NULL, "1.3.6.1.4.1.3495.1");
	assert(m2 == n);

	/* SQ_SYS - 1.3.6.1.4.1.3495.1.1 */
	snmpAddNodeStr("1.3.6.1.4.1.3495.1", 1, NULL, NULL);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.1", 1, snmp_sysFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.1", 2, snmp_sysFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.1", 3, snmp_sysFn, static_Inst);

	/* SQ_CONF - 1.3.6.1.4.1.3495.1.2 */
	snmpAddNodeStr("1.3.6.1.4.1.3495.1", 2, NULL, NULL);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.2", CONF_ADMIN, snmp_confFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.2", CONF_VERSION, snmp_confFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.2", CONF_VERSION_ID, snmp_confFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.2", CONF_LOG_FAC, snmp_confFn, static_Inst);

	/* SQ_CONF + CONF_STORAGE - 1.3.6.1.4.1.3495.1.5 */
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.2", CONF_STORAGE, NULL, NULL);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.2.5", 1, snmp_confFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.2.5", 2, snmp_confFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.2.5", 3, snmp_confFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.2.5", 4, snmp_confFn, static_Inst);

	snmpAddNodeStr("1.3.6.1.4.1.3495.1.2", CONF_UNIQNAME, snmp_confFn, static_Inst);

	/* SQ_PRF - 1.3.6.1.4.1.3495.1.3 */
	snmpAddNodeStr("1.3.6.1.4.1.3495.1", 3, NULL, NULL);			/* SQ_PRF */

	/* PERF_SYS - 1.3.6.1.4.1.3495.1.3.1 */
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3", PERF_SYS, NULL, NULL);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.1", PERF_SYS_PF, snmp_prfSysFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.1", PERF_SYS_NUMR, snmp_prfSysFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.1", PERF_SYS_MEMUSAGE, snmp_prfSysFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.1", PERF_SYS_CPUTIME, snmp_prfSysFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.1", PERF_SYS_CPUUSAGE, snmp_prfSysFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.1", PERF_SYS_MAXRESSZ, snmp_prfSysFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.1", PERF_SYS_NUMOBJCNT, snmp_prfSysFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.1", PERF_SYS_CURLRUEXP, snmp_prfSysFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.1", PERF_SYS_CURUNLREQ, snmp_prfSysFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.1", PERF_SYS_CURUNUSED_FD, snmp_prfSysFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.1", PERF_SYS_CURRESERVED_FD, snmp_prfSysFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.1", PERF_SYS_CURUSED_FD, snmp_prfSysFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.1", PERF_SYS_CURMAX_FD, snmp_prfSysFn, static_Inst);

	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3", PERF_PROTO, NULL, NULL);

	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2", PERF_PROTOSTAT_AGGR, NULL, NULL);

	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.1", PERF_PROTOSTAT_AGGR_HTTP_REQ, snmp_prfProtoFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.1", PERF_PROTOSTAT_AGGR_HTTP_HITS, snmp_prfProtoFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.1", PERF_PROTOSTAT_AGGR_HTTP_ERRORS, snmp_prfProtoFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.1", PERF_PROTOSTAT_AGGR_HTTP_KBYTES_IN, snmp_prfProtoFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.1", PERF_PROTOSTAT_AGGR_HTTP_KBYTES_OUT, snmp_prfProtoFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.1", PERF_PROTOSTAT_AGGR_ICP_S, snmp_prfProtoFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.1", PERF_PROTOSTAT_AGGR_ICP_R, snmp_prfProtoFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.1", PERF_PROTOSTAT_AGGR_ICP_SKB, snmp_prfProtoFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.1", PERF_PROTOSTAT_AGGR_ICP_RKB, snmp_prfProtoFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.1", PERF_PROTOSTAT_AGGR_REQ, snmp_prfProtoFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.1", PERF_PROTOSTAT_AGGR_ERRORS, snmp_prfProtoFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.1", PERF_PROTOSTAT_AGGR_KBYTES_IN, snmp_prfProtoFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.1", PERF_PROTOSTAT_AGGR_KBYTES_OUT, snmp_prfProtoFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.1", PERF_PROTOSTAT_AGGR_CURSWAP, snmp_prfProtoFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.1", PERF_PROTOSTAT_AGGR_CLIENTS, snmp_prfProtoFn, static_Inst);

	/* Note this is time-series rather than 'static' */
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2", PERF_PROTOSTAT_MEDIAN, NULL, NULL);
	/* Not sure what this is.. cacheMedianSvcEntry ? */
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.2", 1, NULL, NULL);

	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.2.1", PERF_MEDIAN_TIME, snmp_prfProtoFn, time_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.2.1", PERF_MEDIAN_HTTP_ALL, snmp_prfProtoFn, time_Inst); 
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.2.1", PERF_MEDIAN_HTTP_MISS, snmp_prfProtoFn, time_Inst);  
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.2.1", PERF_MEDIAN_HTTP_NM, snmp_prfProtoFn, time_Inst); 
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.2.1", PERF_MEDIAN_HTTP_HIT, snmp_prfProtoFn, time_Inst); 
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.2.1", PERF_MEDIAN_ICP_QUERY, snmp_prfProtoFn, time_Inst); 
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.2.1", PERF_MEDIAN_ICP_REPLY, snmp_prfProtoFn, time_Inst); 
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.2.1", PERF_MEDIAN_DNS, snmp_prfProtoFn, time_Inst); 
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.2.1", PERF_MEDIAN_RHR, snmp_prfProtoFn, time_Inst); 
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.2.1", PERF_MEDIAN_BHR, snmp_prfProtoFn, time_Inst); 
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.3.2.2.1", PERF_MEDIAN_HTTP_NH, snmp_prfProtoFn, time_Inst); 

	/* SQ_NET - 1.3.6.1.4.1.3495.1.4 */
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.", 4, NULL, NULL);

	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4", NET_IP_CACHE, NULL, NULL);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.1", IP_ENT, snmp_netIpFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.1", IP_REQ, snmp_netIpFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.1", IP_HITS, snmp_netIpFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.1", IP_PENDHIT, snmp_netIpFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.1", IP_NEGHIT, snmp_netIpFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.1", IP_MISS, snmp_netIpFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.1", IP_GHBN, snmp_netIpFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.1", IP_LOC, snmp_netIpFn, static_Inst);

	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4", NET_FQDN_CACHE, NULL, NULL);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.2", FQDN_ENT, snmp_netFqdnFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.2", FQDN_REQ, snmp_netFqdnFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.2", FQDN_HITS, snmp_netFqdnFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.2", FQDN_PENDHIT, snmp_netFqdnFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.2", FQDN_NEGHIT, snmp_netFqdnFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.2", FQDN_MISS, snmp_netFqdnFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.2", FQDN_GHBN, snmp_netFqdnFn, static_Inst);

	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.3", DNS_REQ, snmp_netIdnsFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.3", DNS_REP, snmp_netIdnsFn, static_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.4.3", DNS_SERVERS, snmp_netIdnsFn, static_Inst);

	/* SQ_MESH - 1.3.6.1.4.1.3495.1.5 */
	snmpAddNodeStr("1.3.6.1.4.1.3495.1", 5, NULL, NULL);

	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5", 1, NULL, NULL);

	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1", 1, NULL, NULL);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.1", 1, snmp_meshPtblFn, peer_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.1", 2, snmp_meshPtblFn, peer_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.1", 3, snmp_meshPtblFn, peer_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.1", 4, snmp_meshPtblFn, peer_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.1", 5, snmp_meshPtblFn, peer_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.1", 6, snmp_meshPtblFn, peer_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.1", 7, snmp_meshPtblFn, peer_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.1", 8, snmp_meshPtblFn, peer_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.1", 9, snmp_meshPtblFn, peer_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.1", 10, snmp_meshPtblFn, peer_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.1", 11, snmp_meshPtblFn, peer_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.1", 12, snmp_meshPtblFn, peer_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.1", 13, snmp_meshPtblFn, peer_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.1", 15, snmp_meshPtblFn, peer_Inst);

	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1", 2, NULL, NULL);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.2", 1, snmp_meshPtblFn, peer_InstIndex);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.2", 2, snmp_meshPtblFn, peer_InstIndex);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.2", 3, snmp_meshPtblFn, peer_InstIndex);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.2", 4, snmp_meshPtblFn, peer_InstIndex);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.2", 5, snmp_meshPtblFn, peer_InstIndex);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.2", 6, snmp_meshPtblFn, peer_InstIndex);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.2", 7, snmp_meshPtblFn, peer_InstIndex);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.2", 8, snmp_meshPtblFn, peer_InstIndex);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.2", 9, snmp_meshPtblFn, peer_InstIndex);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.2", 10, snmp_meshPtblFn, peer_InstIndex);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.2", 11, snmp_meshPtblFn, peer_InstIndex);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.2", 12, snmp_meshPtblFn, peer_InstIndex);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.2", 13, snmp_meshPtblFn, peer_InstIndex);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.2", 14, snmp_meshPtblFn, peer_InstIndex);
	mib_tree_last = snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.1.2", 15, snmp_meshPtblFn, peer_InstIndex);

#if 0
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5", 2, NULL, NULL);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.2", 1, NULL, NULL);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.2.1", 1, snmp_meshCtblFn, client_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.2.1", 2, snmp_meshCtblFn, client_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.2.1", 3, snmp_meshCtblFn, client_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.2.1", 4, snmp_meshCtblFn, client_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.2.1", 5, snmp_meshCtblFn, client_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.2.1", 6, snmp_meshCtblFn, client_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.2.1", 7, snmp_meshCtblFn, client_Inst);
	snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.2.1", 8, snmp_meshCtblFn, client_Inst);
	mib_tree_last = snmpAddNodeStr("1.3.6.1.4.1.3495.1.5.2.1", 9, snmp_meshCtblFn, client_Inst);
#endif

    debug(49, 9) ("snmpInit: Completed SNMP mib tree structure\n");
}

void
snmpConnectionOpen(void)
{
    u_short port;
    struct sockaddr_in xaddr;
    socklen_t len;
    int x;

    debug(49, 5) ("snmpConnectionOpen: Called\n");
    if ((port = Config.Port.snmp) > (u_short) 0) {
	enter_suid();
	theInSnmpConnection = comm_open(SOCK_DGRAM,
	    IPPROTO_UDP,
	    Config.Addrs.snmp_incoming,
	    port,
	    COMM_NONBLOCKING,
	    COMM_TOS_DEFAULT,
	    "SNMP Port");
	leave_suid();
	if (theInSnmpConnection < 0)
	    fatal("Cannot open snmp Port");
	commSetSelect(theInSnmpConnection, COMM_SELECT_READ, snmpHandleUdp, NULL, 0);
	debug(1, 1) ("Accepting SNMP messages on port %d, FD %d.\n",
	    (int) port, theInSnmpConnection);
	if (! IsNoAddr(&Config.Addrs.snmp_outgoing)) {
	    enter_suid();
	    theOutSnmpConnection = comm_open(SOCK_DGRAM,
		IPPROTO_UDP,
		Config.Addrs.snmp_outgoing,
		port,
		COMM_NONBLOCKING,
	        COMM_TOS_DEFAULT,
		"SNMP Port");
	    leave_suid();
	    if (theOutSnmpConnection < 0)
		fatal("Cannot open Outgoing SNMP Port");
	    commSetSelect(theOutSnmpConnection,
		COMM_SELECT_READ,
		snmpHandleUdp,
		NULL, 0);
	    debug(1, 1) ("Outgoing SNMP messages on port %d, FD %d.\n",
		(int) port, theOutSnmpConnection);
	    fd_note(theOutSnmpConnection, "Outgoing SNMP socket");
	    fd_note(theInSnmpConnection, "Incoming SNMP socket");
	} else {
	    theOutSnmpConnection = theInSnmpConnection;
	}
	memset(&theOutSNMPAddr, '\0', sizeof(struct in_addr));
	len = sizeof(struct sockaddr_in);
	memset(&xaddr, '\0', len);
	x = getsockname(theOutSnmpConnection,
	    (struct sockaddr *) &xaddr, &len);
	if (x < 0)
	    debug(51, 1) ("theOutSnmpConnection FD %d: getsockname: %s\n",
		theOutSnmpConnection, xstrerror());
	else
	    theOutSNMPAddr = xaddr.sin_addr;
    }
}

void
snmpConnectionShutdown(void)
{
    if (theInSnmpConnection < 0)
	return;
    if (theInSnmpConnection != theOutSnmpConnection) {
	debug(49, 1) ("FD %d Closing SNMP socket\n", theInSnmpConnection);
	comm_close(theInSnmpConnection);
    }
    /*
     * Here we set 'theInSnmpConnection' to -1 even though the SNMP 'in'
     * and 'out' sockets might be just one FD.  This prevents this
     * function from executing repeatedly.  When we are really ready to
     * exit or restart, main will comm_close the 'out' descriptor.
     */ theInSnmpConnection = -1;
    /*
     * Normally we only write to the outgoing SNMP socket, but we
     * also have a read handler there to catch messages sent to that
     * specific interface.  During shutdown, we must disable reading
     * on the outgoing socket.
     */
    assert(theOutSnmpConnection > -1);
    commSetSelect(theOutSnmpConnection, COMM_SELECT_READ, NULL, NULL, 0);
}

void
snmpConnectionClose(void)
{
    snmpConnectionShutdown();
    if (theOutSnmpConnection > -1) {
	debug(49, 1) ("FD %d Closing SNMP socket\n", theOutSnmpConnection);
	comm_close(theOutSnmpConnection);
    }
}

/*
 * Functions for handling the requests.
 */

/*
 * Accept the UDP packet
 */
void
snmpHandleUdp(int sock, void *not_used)
{
    LOCAL_ARRAY(char, buf, SNMP_REQUEST_SIZE);
    struct sockaddr_in from;
    socklen_t from_len;
    snmp_request_t *snmp_rq;
    int len;

    debug(49, 5) ("snmpHandleUdp: Called.\n");

    commSetSelect(sock, COMM_SELECT_READ, snmpHandleUdp, NULL, 0);
    from_len = sizeof(struct sockaddr_in);
    memset(&from, '\0', from_len);
    memset(buf, '\0', SNMP_REQUEST_SIZE);

    CommStats.syscalls.sock.recvfroms++;

    len = recvfrom(sock,
	buf,
	SNMP_REQUEST_SIZE,
	0,
	(struct sockaddr *) &from,
	&from_len);

    if (len > 0) {
	buf[len] = '\0';
	debug(49, 3) ("snmpHandleUdp: FD %d: received %d bytes from %s.\n",
	    sock,
	    len,
	    inet_ntoa(from.sin_addr));

	snmp_rq = xcalloc(1, sizeof(snmp_request_t));
	snmp_rq->buf = (u_char *) buf;
	snmp_rq->len = len;
	snmp_rq->sock = sock;
	snmp_rq->outbuf = xmalloc(snmp_rq->outlen = SNMP_REQUEST_SIZE);
	xmemcpy(&snmp_rq->from, &from, sizeof(struct sockaddr_in));
	snmpDecodePacket(snmp_rq);
	xfree(snmp_rq->outbuf);
	xfree(snmp_rq);
    } else {
	debug(49, 1) ("snmpHandleUdp: FD %d recvfrom: %s\n", sock, xstrerror());
    }
}

/*
 * Turn SNMP packet into a PDU, check available ACL's
 */
static void
snmpDecodePacket(snmp_request_t * rq)
{
    struct snmp_pdu *PDU;
    aclCheck_t checklist;
    u_char *Community;
    u_char *buf = rq->buf;
    int len = rq->len;
    int allow = 0;

    debug(49, 5) ("snmpDecodePacket: Called.\n");
    /* Now that we have the data, turn it into a PDU */
    PDU = snmp_pdu_create(0);
    rq->session.Version = SNMP_VERSION_1;
    Community = snmp_parse(&rq->session, PDU, buf, len);
    memset(&checklist, '\0', sizeof(checklist));
    checklist.src_addr = rq->from.sin_addr;
    checklist.snmp_community = (char *) Community;

    if (Community)
	allow = aclCheckFast(Config.accessList.snmp, &checklist);
    if ((snmp_coexist_V2toV1(PDU)) && (Community) && (allow)) {
	rq->community = Community;
	rq->PDU = PDU;
	debug(49, 5) ("snmpDecodePacket: reqid=[%d]\n", PDU->reqid);
	snmpConstructReponse(rq);
    } else {
	debug(49, 1) ("Failed SNMP agent query from : %s.\n",
	    inet_ntoa(rq->from.sin_addr));
	snmp_free_pdu(PDU);
    }
    if (Community)
	xfree(Community);
}

/*
 * Packet OK, ACL Check OK, Create reponse.
 */
static void
snmpConstructReponse(snmp_request_t * rq)
{
    struct snmp_pdu *RespPDU;

    debug(49, 5) ("snmpConstructReponse: Called.\n");
    RespPDU = snmpAgentResponse(rq->PDU);
    snmp_free_pdu(rq->PDU);
    if (RespPDU != NULL) {
	snmp_build(&rq->session, RespPDU, rq->outbuf, &rq->outlen);
	comm_udp_sendto(rq->sock, &rq->from, sizeof(rq->from), rq->outbuf, rq->outlen);
	snmp_free_pdu(RespPDU);
    }
}

/*
 * Decide how to respond to the request, construct a response and
 * return the response to the requester.
 */
static struct snmp_pdu *
snmpAgentResponse(struct snmp_pdu *PDU)
{
    struct snmp_pdu *Answer = NULL;

    debug(49, 5) ("snmpAgentResponse: Called.\n");

    if ((Answer = snmp_pdu_create(SNMP_PDU_RESPONSE))) {
	Answer->reqid = PDU->reqid;
	Answer->errindex = 0;
	if (PDU->command == SNMP_PDU_GET || PDU->command == SNMP_PDU_GETNEXT) {
	    int get_next = (PDU->command == SNMP_PDU_GETNEXT);
	    variable_list *VarPtr_;
	    variable_list **RespVars = &(Answer->variables);
	    oid_ParseFn *ParseFn;
	    int index = 0;
	    /* Loop through all variables */
	    for (VarPtr_ = PDU->variables; VarPtr_; VarPtr_ = VarPtr_->next_variable) {
		variable_list *VarPtr = VarPtr_;
		variable_list *VarNew = NULL;
		oid *NextOidName = NULL;
		snint NextOidNameLen = 0;

		index++;

		/* Find the parsing function for this variable */
		if (get_next)
		    ParseFn = snmpTreeNext(VarPtr->name, VarPtr->name_length, &NextOidName, &NextOidNameLen);
		else
		    ParseFn = snmpTreeGet(VarPtr->name, VarPtr->name_length);
		if (ParseFn == NULL) {
		    Answer->errstat = SNMP_ERR_NOSUCHNAME;
		    debug(49, 5) ("snmpAgentResponse: No such oid.\n");
		} else {
		    int *errstatTmp;
		    if (get_next) {
			VarPtr = snmp_var_new(NextOidName, NextOidNameLen);
			xfree(NextOidName);
		    }
		    errstatTmp = &(Answer->errstat);
		    VarNew = (*ParseFn) (VarPtr, (snint *) errstatTmp);
		    if (get_next)
			snmp_var_free(VarPtr);
		}

		/* Was there an error? */
		if ((Answer->errstat != SNMP_ERR_NOERROR) || (VarNew == NULL)) {
		    Answer->errindex = index;
		    debug(49, 5) ("snmpAgentResponse: error.\n");
		    if (VarNew)
			snmp_var_free(VarNew);
		    /* Free the already processed results, if any */
		    while ((VarPtr = Answer->variables) != NULL) {
			Answer->variables = VarPtr->next_variable;
			snmp_var_free(VarPtr);
		    }
		    /* Steal the original PDU list of variables for the error response */
		    Answer->variables = PDU->variables;
		    PDU->variables = NULL;
		    return (Answer);
		}
		/* No error.  Insert this var at the end, and move on to the next.
		 */
		*RespVars = VarNew;
		RespVars = &(VarNew->next_variable);
	    }
	}
    }
    return (Answer);
}

static oid_ParseFn *
snmpTreeGet(oid * Current, snint CurrentLen)
{
    oid_ParseFn *Fn = NULL;
    mib_tree_entry *mibTreeEntry = NULL;
    int count = 0;

    debug(49, 5) ("snmpTreeGet: Called\n");

    debug(49, 6) ("snmpTreeGet: Current : \n");
    snmpDebugOid(6, Current, CurrentLen);

    mibTreeEntry = mib_tree_head;
    if (Current[count] == mibTreeEntry->name[count]) {
	count++;
	while ((mibTreeEntry) && (count < CurrentLen) && (!mibTreeEntry->parsefunction)) {
	    mibTreeEntry = snmpTreeEntry(Current[count], count, mibTreeEntry);
	    count++;
	}
    }
    if (mibTreeEntry && mibTreeEntry->parsefunction)
	Fn = mibTreeEntry->parsefunction;
    debug(49, 5) ("snmpTreeGet: return\n");
    return (Fn);
}

static oid_ParseFn *
snmpTreeNext(oid * Current, snint CurrentLen, oid ** Next, snint * NextLen)
{
    oid_ParseFn *Fn = NULL;
    mib_tree_entry *mibTreeEntry = NULL, *nextoid = NULL;
    int count = 0;

    debug(49, 5) ("snmpTreeNext: Called\n");

    debug(49, 6) ("snmpTreeNext: Current : \n");
    snmpDebugOid(6, Current, CurrentLen);

    mibTreeEntry = mib_tree_head;
    if (Current[count] == mibTreeEntry->name[count]) {
	count++;
	while ((mibTreeEntry) && (count < CurrentLen) && (!mibTreeEntry->parsefunction)) {
	    mib_tree_entry *nextmibTreeEntry = snmpTreeEntry(Current[count], count, mibTreeEntry);
	    if (!nextmibTreeEntry)
		break;
	    else
		mibTreeEntry = nextmibTreeEntry;
	    count++;
	}
	debug(49, 5) ("snmpTreeNext: Recursed down to requested object\n");
    } else {
	return NULL;
    }
    if (mibTreeEntry == mib_tree_last)
	return (Fn);
    if ((mibTreeEntry) && (mibTreeEntry->parsefunction)) {
	*NextLen = CurrentLen;
	*Next = (*mibTreeEntry->instancefunction) (Current, NextLen, mibTreeEntry, &Fn);
	if (*Next)
	    return (Fn);
    }
    if ((mibTreeEntry) && (mibTreeEntry->parsefunction)) {
	count--;
	nextoid = snmpTreeSiblingEntry(Current[count], count, mibTreeEntry->parent);
	if (nextoid) {
	    debug(49, 5) ("snmpTreeNext: Next OID found for sibling\n");
	    mibTreeEntry = nextoid;
	    count++;
	} else {
	    debug(49, 5) ("snmpTreeNext: Attempting to recurse up for next object\n");
	    while (!nextoid) {
		count--;
		if (mibTreeEntry->parent->parent) {
		    nextoid = mibTreeEntry->parent;
		    mibTreeEntry = snmpTreeEntry(Current[count] + 1, count, nextoid->parent);
		    if (!mibTreeEntry) {
			mibTreeEntry = nextoid;
			nextoid = NULL;
		    }
		} else {
		    nextoid = mibTreeEntry;
		    mibTreeEntry = NULL;
		}
	    }
	}
    }
    while ((mibTreeEntry) && (!mibTreeEntry->parsefunction)) {
	mibTreeEntry = mibTreeEntry->leaves[0];
    }
    if (mibTreeEntry) {
	*NextLen = mibTreeEntry->len;
	*Next = (*mibTreeEntry->instancefunction) (mibTreeEntry->name, NextLen, mibTreeEntry, &Fn);
    }
    if (*Next)
	return (Fn);
    else
	return NULL;
}

static oid *
static_Inst(oid * name, snint * len, mib_tree_entry * current, oid_ParseFn ** Fn)
{
    oid *instance = NULL;

    if (*len <= current->len) {
	instance = xmalloc(sizeof(name) * (*len + 1));
	xmemcpy(instance, name, (sizeof(name) * *len));
	instance[*len] = 0;
	*len += 1;
    }
    *Fn = current->parsefunction;
    return (instance);
}

static oid *
time_Inst(oid * name, snint * len, mib_tree_entry * current, oid_ParseFn ** Fn)
{
    oid *instance = NULL;
    int identifier = 0, loop = 0;
    int index[TIME_INDEX_LEN] =
    {TIME_INDEX};

    if (*len <= current->len) {
	instance = xmalloc(sizeof(name) * (*len + 1));
	xmemcpy(instance, name, (sizeof(name) * *len));
	instance[*len] = *index;
	*len += 1;
    } else {
	identifier = name[*len - 1];
	while ((loop < TIME_INDEX_LEN) && (identifier != index[loop]))
	    loop++;
	if (loop < TIME_INDEX_LEN - 1) {
	    instance = xmalloc(sizeof(name) * (*len));
	    xmemcpy(instance, name, (sizeof(name) * *len));
	    instance[*len - 1] = index[++loop];
	}
    }
    *Fn = current->parsefunction;
    return (instance);
}

static oid *
peer_Inst(oid * name, snint * len, mib_tree_entry * current, oid_ParseFn ** Fn)
{
    oid *instance = NULL;
    u_char *cp = NULL;
    peer *peerptr = Config.peers;
    peer *peerptr2;
    struct in_addr *laddr = NULL;
    char *host_addr = NULL, *current_addr = NULL, *last_addr = NULL;

    if (peerptr == NULL) {
	/* Do nothing */
    } else if (*len <= current->len) {
	instance = xmalloc(sizeof(name) * (*len + 4));
	xmemcpy(instance, name, (sizeof(name) * *len));
	cp = (u_char *) & (peerptr->in_addr.sin_addr.s_addr);
	instance[*len] = *cp++;
	instance[*len + 1] = *cp++;
	instance[*len + 2] = *cp++;
	instance[*len + 3] = *cp++;
	*len += 4;
    } else {
	laddr = oid2addr(&name[*len - 4]);
	host_addr = inet_ntoa(*laddr);
	last_addr = xstrdup(host_addr);
      skip_duplicate:
	current_addr = inet_ntoa(peerptr->in_addr.sin_addr);
	while (peerptr && strcmp(last_addr, current_addr) != 0) {
	    peerptr = peerptr->next;
	    if (peerptr)
		current_addr = inet_ntoa(peerptr->in_addr.sin_addr);
	}

	/* Find the next peer */
	if (peerptr)
	    peerptr = peerptr->next;

	/* watch out for duplicate addresses */
	for (peerptr2 = Config.peers; peerptr && peerptr2 != peerptr; peerptr2 = peerptr2->next) {
	    if (peerptr2->in_addr.sin_addr.s_addr == peerptr->in_addr.sin_addr.s_addr) {
		/* ouch.. there are more than one peer on this IP. Skip the second one */
		peerptr = peerptr->next;
		if (peerptr)
		    goto skip_duplicate;
	    }
	}

	xfree(last_addr);

	if (peerptr) {
	    instance = xmalloc(sizeof(name) * (*len));
	    xmemcpy(instance, name, (sizeof(name) * *len));
	    cp = (u_char *) & (peerptr->in_addr.sin_addr.s_addr);
	    instance[*len - 4] = *cp++;
	    instance[*len - 3] = *cp++;
	    instance[*len - 2] = *cp++;
	    instance[*len - 1] = *cp++;
	}
    }
    *Fn = current->parsefunction;
    return (instance);
}

static oid *
peer_InstIndex(oid * name, snint * len, mib_tree_entry * current, oid_ParseFn ** Fn)
{
    oid *instance = NULL;

    if (!Config.peers) {
	/* Do nothing */
    } else if (*len <= current->len) {
	instance = xmalloc(sizeof(name) * (*len + 1));
	xmemcpy(instance, name, (sizeof(name) * *len));
	instance[*len] = 1;
	*len += 1;
    } else {
	int identifier, loop = 1;	/* our index starts at 1 */
	peer *p = Config.peers;
	identifier = name[*len - 1];

	/* We want the next one... */
	identifier += 1;

	/* Make sure it exists */
	while ((identifier != loop) && (p != NULL)) {
	    loop++;
	    p = p->next;
	}
	if (p != NULL) {
	    instance = xmalloc(sizeof(name) * (*len));
	    xmemcpy(instance, name, (sizeof(name) * *len));
	    instance[*len - 1] = loop;
	}
    }
    *Fn = current->parsefunction;
    return (instance);
}

#if 0
static oid *
client_Inst(oid * name, snint * len, mib_tree_entry * current, oid_ParseFn ** Fn)
{
    oid *instance = NULL;
    u_char *cp = NULL;
    struct in_addr *laddr = NULL;

    if (*len <= current->len) {
	instance = xmalloc(sizeof(name) * (*len + 4));
	xmemcpy(instance, name, (sizeof(name) * *len));
	laddr = client_entry(NULL);
	if (laddr) {
	    cp = (u_char *) & (laddr->s_addr);
	    instance[*len] = *cp++;
	    instance[*len + 1] = *cp++;
	    instance[*len + 2] = *cp++;
	    instance[*len + 3] = *cp++;
	    *len += 4;
	}
    } else {
	laddr = oid2addr(&name[*len - 4]);
	laddr = client_entry(laddr);
	if (laddr) {
	    instance = xmalloc(sizeof(name) * (*len));
	    xmemcpy(instance, name, (sizeof(name) * *len));
	    cp = (u_char *) & (laddr->s_addr);
	    instance[*len - 4] = *cp++;
	    instance[*len - 3] = *cp++;
	    instance[*len - 2] = *cp++;
	    instance[*len - 1] = *cp++;
	}
    }
    *Fn = current->parsefunction;
    return (instance);
}
#endif


/*
 * Utility functions
 */

/*
 * Tree utility functions. 
 */

/* 
 * Returns a the sibling object in the tree
 */
static mib_tree_entry *
snmpTreeSiblingEntry(oid entry, snint len, mib_tree_entry * current)
{
    mib_tree_entry *next = NULL;
    int count = 0;

    while ((!next) && (count < current->children)) {
	if (current->leaves[count]->name[len] == entry) {
	    next = current->leaves[count];
	}
	count++;
    }
    if (count < current->children) {
	next = current->leaves[count];
    } else {
	next = NULL;
    }
    return (next);
}

/* 
 * Returns the requested child object or NULL if it does not exist
 */
static mib_tree_entry *
snmpTreeEntry(oid entry, snint len, mib_tree_entry * current)
{
    mib_tree_entry *next = NULL;
    int count = 0;

    while ((!next) && (count < current->children)) {
	if (current->leaves[count]->name[len] == entry) {
	    next = current->leaves[count];
	}
	count++;
    }
    return (next);
}

void
snmpAddNodeChild(mib_tree_entry *entry, mib_tree_entry *child)
{
	debug (49, 5) ("snmpAddNodeChild: assigning %p to parent %p\n", child, entry);
        entry->leaves = xrealloc(entry->leaves, sizeof(mib_tree_entry *) * (entry->children + 1));
	entry->leaves[entry->children] = child;
	entry->leaves[entry->children]->parent = entry;
	entry->children++;
}

mib_tree_entry *
snmpLookupNodeStr(mib_tree_entry *root, const char *str)
{
	oid *name;
	int namelen;
	int r, i;
	mib_tree_entry *e;

	if (root)
		e = root;
	else
		e = mib_tree_head;

	if (! snmpCreateOidFromStr(str, &name, &namelen))
		return NULL;

	/* I wish there were some kind of sensible existing tree traversal
	 * routine to use. I'll worry about that later */
	if (namelen <= 1) {
		xfree(name);
		return e;	/* XXX it should only be this? */
	}

	r = 1;
	while(r <= namelen) {
		/* Find the child node which matches this */
		for (i = 0; i < e->children && e->leaves[i]->name[r] != name[r]; i++)
			;
		/* Are we pointing to that node? */
		if (i >= e->children)
			break;
		assert(e->leaves[i]->name[r] == name[r]);

		/* Skip to that node! */
		e = e->leaves[i];
		r++;
	}

	xfree(name);
	return e;
}


int
snmpCreateOidFromStr(const char *str, oid **name, int *nl)
{
	char *delim = ".";
	char *s;
	char *p;

	*name = NULL;
	*nl = 0;
	s = xstrdup(str);

	/* Parse the OID string into oid bits */
	while ( (p = strsep(&s, delim)) != NULL) {
		*name = xrealloc(*name, sizeof(oid) * ((*nl) + 1));
		(*name)[*nl] = atoi(p);
		(*nl)++;
	}

	xfree(s);
	return 1;
}

/*
 * Create an entry. Return a pointer to the newly created node, or NULL
 * on failure.
 */
static mib_tree_entry *
snmpAddNodeStr(const char *base_str, int o, oid_ParseFn * parsefunction, instance_Fn * instancefunction)
{
	mib_tree_entry *m, *b;
	oid *n;
	int nl;
	char s[1024];

	/* Find base node */
	b = snmpLookupNodeStr(mib_tree_head, base_str);
	if (! b)
		return NULL;
	debug(49, 5) ("snmpAddNodeStr: %s: -> %p\n", base_str, b);

	/* Create OID string for new entry */
	snprintf(s, 1024, "%s.%d", base_str, o);
	if (! snmpCreateOidFromStr(s, &n, &nl))
		return NULL;

	/* Create a node */
	m = snmpAddNode(n, nl, parsefunction, instancefunction, 0);

	/* Link it into the existing tree */
	snmpAddNodeChild(b, m);

	/* Return the node */
	return m;
}

/*
 * Adds a node to the MIB tree structure and adds the appropriate children
 */
static mib_tree_entry *
#if STDC_HEADERS
snmpAddNode(oid * name, int len, oid_ParseFn * parsefunction, instance_Fn * instancefunction, int children,...)
#else
snmpAddNode(va_alist)
     va_dcl
#endif
{
#if STDC_HEADERS
    va_list args;
    int loop;
    mib_tree_entry *entry = NULL;
    va_start(args, children);
#else
    va_list args;
    oid *name = NULL;
    int len = 0, children = 0, loop;
    oid_ParseFn *parsefunction = NULL;
    instance_Fn *instancefunction = NULL;
    mib_tree_entry *entry = NULL;
    va_start(args);
    name = va_arg(args, oid *);
    len = va_arg(args, int);
    parsefunction = va_arg(args, oid_ParseFn *);
    instancefunction = va_arg(args, instance_Fn *);
    children = va_arg(args, int);
#endif

    debug(49, 6) ("snmpAddNode: Children : %d, Oid : \n", children);
    snmpDebugOid(6, name, len);

    va_start(args, children);
    entry = xmalloc(sizeof(mib_tree_entry));
    entry->name = name;
    entry->len = len;
    entry->parsefunction = parsefunction;
    entry->instancefunction = instancefunction;
    entry->children = children;
    entry->leaves = NULL;

    if (children > 0) {
	entry->leaves = xmalloc(sizeof(mib_tree_entry *) * children);
	for (loop = 0; loop < children; loop++) {
	    entry->leaves[loop] = va_arg(args, mib_tree_entry *);
	    entry->leaves[loop]->parent = entry;
	}
    }
    return (entry);
}
/* End of tree utility functions */

/* 
 * Returns the list of parameters in an oid
 */
static oid *
#if STDC_HEADERS
snmpCreateOid(int length,...)
#else
snmpCreateOid(va_alist)
     va_dcl
#endif
{
#if STDC_HEADERS
    va_list args;
    oid *new_oid;
    int loop;
    va_start(args, length);
#else
    va_list args;
    int length = 0, loop;
    oid *new_oid;
    va_start(args);
    length va_arg(args, int);
#endif

    new_oid = xmalloc(sizeof(oid) * length);

    if (length > 0) {
	for (loop = 0; loop < length; loop++) {
	    new_oid[loop] = va_arg(args, int);
	}
    }
    return (new_oid);
}

#if UNUSED_CODE
/*
 * Allocate space for, and copy, an OID.  Returns new oid.
 */
static oid *
snmpOidDup(oid * A, snint ALen)
{
    oid *Ans = xmalloc(sizeof(oid) * ALen);
    xmemcpy(Ans, A, (sizeof(oid) * ALen));
    return Ans;
}
#endif

/*
 * Debug calls, prints out the OID for debugging purposes.
 */
void
snmpDebugOid(int lvl, oid * Name, snint Len)
{
    char mbuf[16];
    int x;
    String objid = StringNull;

    for (x = 0; x < Len; x++) {
	snprintf(mbuf, sizeof(mbuf), ".%u", (unsigned int) Name[x]);
	strCat(objid, mbuf);
    }

    debug(49, lvl) ("   oid = %.*s\n", strLen2(objid), strBuf2(objid));
    stringClean(&objid);
}

static void
snmpSnmplibDebug(int lvl, char *buf)
{
    debug(49, lvl) ("%s\n", buf);
}

void
addr2oid(struct in_addr addr, oid * Dest)
{
    u_char *cp;
    cp = (u_char *) & (addr.s_addr);
    Dest[0] = *cp++;
    Dest[1] = *cp++;
    Dest[2] = *cp++;
    Dest[3] = *cp++;
}

struct in_addr
       *
oid2addr(oid * id)
{
    static struct in_addr laddr;
    u_char *cp = (u_char *) & (laddr.s_addr);
    cp[0] = id[0];
    cp[1] = id[1];
    cp[2] = id[2];
    cp[3] = id[3];
    return &laddr;
}
