
/*
 * $Id: ipcache.c 13314 2008-09-10 12:40:34Z adrian.chadd $
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

#include "squid.h"

static void ipcacheStatPrint(ipcache_entry *, StoreEntry *);

void
ipcache_local_params(void)
{
	//namecache_dns_skiptests = opt_dns_tests;
	namecache_dns_positive_ttl = Config.positiveDnsTtl;
	namecache_dns_negative_ttl = Config.negativeDnsTtl;

	namecache_ipcache_size = Config.ipcache.size;
	namecache_ipcache_high = Config.ipcache.high;
	namecache_ipcache_low = Config.ipcache.low;
}

/* initialize the local ipcache code */
void
ipcache_init_local(void)
{
        static int registered = 0;
        if (! registered) {
                registered = 1;
		cachemgrRegister("ipcache", "IP Cache Stats and Contents", stat_ipcache_get,  NULL, NULL, 0, 1, 0);
	}
}

static void
ipcacheStatPrint(ipcache_entry * i, StoreEntry * sentry)
{
    int k;
    storeAppendPrintf(sentry, " %-32.32s %c%c %6d %6d %2d(%2d)",
	hashKeyStr(&i->hash),
	i->flags.fromhosts ? 'H' : ' ',
	i->flags.negcached ? 'N' : ' ',
	(int) (squid_curtime - i->lastref),
	(int) ((i->flags.fromhosts ? -1 : i->expires - squid_curtime)),
	(int) i->addrs.count,
	(int) i->addrs.badcount);
    for (k = 0; k < (int) i->addrs.count; k++) {
	storeAppendPrintf(sentry, " %15s-%3s", inet_ntoa(i->addrs.in_addrs[k]),
	    i->addrs.bad_mask[k] ? "BAD" : "OK ");
    }
    storeAppendPrintf(sentry, "\n");
}

/* process objects list */
void
stat_ipcache_get(StoreEntry * sentry, void* data)
{
    dlink_node *m;
    assert(ip_table != NULL);
    storeAppendPrintf(sentry, "IP Cache Statistics:\n");
    storeAppendPrintf(sentry, "IPcache Entries:  %d\n",
	memPoolInUseCount(pool_ipcache));
    storeAppendPrintf(sentry, "IPcache Requests: %d\n",
	IpcacheStats.requests);
    storeAppendPrintf(sentry, "IPcache Hits:             %d\n",
	IpcacheStats.hits);
    storeAppendPrintf(sentry, "IPcache Negative Hits:        %d\n",
	IpcacheStats.negative_hits);
    storeAppendPrintf(sentry, "IPcache Numeric Hits:         %d\n",
	IpcacheStats.numeric_hits);
    storeAppendPrintf(sentry, "IPcache Misses:           %d\n",
	IpcacheStats.misses);
    storeAppendPrintf(sentry, "IPcache Invalid Requests: %d\n",
	IpcacheStats.invalid);
    storeAppendPrintf(sentry, "\n\n");
    storeAppendPrintf(sentry, "IP Cache Contents:\n\n");
    storeAppendPrintf(sentry, " %-29.29s %3s %6s %6s %1s\n",
	"Hostname",
	"Flg",
	"lstref",
	"TTL",
	"N");
    for (m = ipcache_lru_list.head; m; m = m->next)
	ipcacheStatPrint(m->data, sentry);
}

#ifdef SQUID_SNMP
/*
 * The function to return the ip cache statistics to via SNMP
 */

variable_list *
snmp_netIpFn(variable_list * Var, snint * ErrP)
{
    variable_list *Answer = NULL;
    debugs(49, 5, "snmp_netIpFn: Processing request:");
    snmpDebugOid(5, Var->name, Var->name_length);
    *ErrP = SNMP_ERR_NOERROR;
    switch (Var->name[LEN_SQ_NET + 1]) {
    case IP_ENT:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    memPoolInUseCount(pool_ipcache),
	    SMI_GAUGE32);
	break;
    case IP_REQ:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    IpcacheStats.requests,
	    SMI_COUNTER32);
	break;
    case IP_HITS:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    IpcacheStats.hits,
	    SMI_COUNTER32);
	break;
    case IP_PENDHIT:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    0,			/* deprecated */
	    SMI_GAUGE32);
	break;
    case IP_NEGHIT:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    IpcacheStats.negative_hits,
	    SMI_COUNTER32);
	break;
    case IP_MISS:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    IpcacheStats.misses,
	    SMI_COUNTER32);
	break;
    case IP_GHBN:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    0,			/* deprecated */
	    SMI_COUNTER32);
	break;
    case IP_LOC:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    0,			/* deprecated */
	    SMI_COUNTER32);
	break;
    default:
	*ErrP = SNMP_ERR_NOSUCHNAME;
	snmp_var_free(Answer);
	return (NULL);
    }
    return Answer;
}

#endif /*SQUID_SNMP */
