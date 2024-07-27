
/*
 * $Id: fqdncache.c 13314 2008-09-10 12:40:34Z adrian.chadd $
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

#include "squid.h"

void fqdnStats(StoreEntry * sentry, void* data);

void
fqdncache_local_params(void)
{
	namecache_fqdncache_size = Config.fqdncache.size;
	namecache_fqdncache_logfqdn = Config.onoff.log_fqdn;
}

/* initialize the local fqdncache code */
void
fqdncache_init_local(void)
{
	static int registered = 0;
	if (! registered) {
		registered = 1;
		cachemgrRegister("fqdncache", "FQDN Cache Stats and Contents", fqdnStats, NULL, NULL, 0, 1, 0);
 	}
}

/* process objects list */
void
fqdnStats(StoreEntry * sentry, void* data)
{
    fqdncache_entry *f = NULL;
    int k;
    int ttl;
    if (fqdn_table == NULL)
	return;
    storeAppendPrintf(sentry, "FQDN Cache Statistics:\n");
    storeAppendPrintf(sentry, "FQDNcache Entries: %d\n",
	memPoolInUseCount(pool_fqdncache));
    storeAppendPrintf(sentry, "FQDNcache Requests: %d\n",
	FqdncacheStats.requests);
    storeAppendPrintf(sentry, "FQDNcache Hits: %d\n",
	FqdncacheStats.hits);
    storeAppendPrintf(sentry, "FQDNcache Negative Hits: %d\n",
	FqdncacheStats.negative_hits);
    storeAppendPrintf(sentry, "FQDNcache Misses: %d\n",
	FqdncacheStats.misses);
    storeAppendPrintf(sentry, "FQDN Cache Contents:\n\n");
    storeAppendPrintf(sentry, "%-15.15s %3s %3s %3s %s\n",
	"Address", "Flg", "TTL", "Cnt", "Hostnames");
    hash_first(fqdn_table);
    while ((f = (fqdncache_entry *) hash_next(fqdn_table))) {
	ttl = (f->flags.fromhosts ? -1 : (f->expires - squid_curtime));
	storeAppendPrintf(sentry, "%-15.15s  %c%c %3.3d % 3d",
	    hashKeyStr(&f->hash),
	    f->flags.negcached ? 'N' : ' ',
	    f->flags.fromhosts ? 'H' : ' ',
	    ttl,
	    (int) f->name_count);
	for (k = 0; k < (int) f->name_count; k++)
	    storeAppendPrintf(sentry, " %s", f->names[k]);
	storeAppendPrintf(sentry, "\n");
    }
}

#ifdef SQUID_SNMP
/*
 * The function to return the fqdn statistics via SNMP
 */

variable_list *
snmp_netFqdnFn(variable_list * Var, snint * ErrP)
{
    variable_list *Answer = NULL;
    debugs(49, 5, "snmp_netFqdnFn: Processing request:");
    snmpDebugOid(5, Var->name, Var->name_length);
    *ErrP = SNMP_ERR_NOERROR;
    switch (Var->name[LEN_SQ_NET + 1]) {
    case FQDN_ENT:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    memPoolInUseCount(pool_fqdncache),
	    SMI_GAUGE32);
	break;
    case FQDN_REQ:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    FqdncacheStats.requests,
	    SMI_COUNTER32);
	break;
    case FQDN_HITS:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    FqdncacheStats.hits,
	    SMI_COUNTER32);
	break;
    case FQDN_PENDHIT:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    0,			/* deprecated */
	    SMI_GAUGE32);
	break;
    case FQDN_NEGHIT:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    FqdncacheStats.negative_hits,
	    SMI_COUNTER32);
	break;
    case FQDN_MISS:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    FqdncacheStats.misses,
	    SMI_COUNTER32);
	break;
    case FQDN_GHBN:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    0,			/* deprecated */
	    SMI_COUNTER32);
	break;
    default:
	*ErrP = SNMP_ERR_NOSUCHNAME;
	break;
    }
    return Answer;
}

#endif /*SQUID_SNMP */
