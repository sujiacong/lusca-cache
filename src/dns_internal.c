
/*
 * $Id: dns_internal.c 14596 2010-04-12 07:02:45Z radiant@aol.jp $
 *
 * DEBUG: section 78    DNS lookups; interacts with lib/rfc1035.c
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

/* MS VisualStudio Projects are monolithic, so we need the following
 * #ifndef to exclude the internal DNS code from compile process when
 * using External DNS process.
 */

#if HAVE_ARPA_NAMESER_H
#include <arpa/nameser.h>
#endif
#if HAVE_RESOLV_H
#include <resolv.h>
#endif

#ifdef _SQUID_WIN32_
#include <windows.h>
#endif
#ifndef _PATH_RESCONF
#define _PATH_RESCONF "/etc/resolv.conf"
#endif

static void idnsParseNameservers(void);
#ifndef _SQUID_MSWIN_
static void idnsParseResolvConf(void);
#endif
#ifdef _SQUID_WIN32_
static void idnsParseWIN32Registry(void);
static void idnsParseWIN32SearchList(const char *);
#endif

static void
idnsParseNameservers(void)
{
    wordlist *w;
    for (w = Config.dns_nameservers; w; w = w->next) {
	debug(78, 1) ("Adding nameserver %s from squid.conf\n", w->key);
	idnsAddNameserver(w->key);
    }
}

#ifndef _SQUID_MSWIN_
static void
idnsParseResolvConf(void)
{
    FILE *fp;
    char buf[RESOLV_BUFSZ];
    const char *t;
    fp = fopen(_PATH_RESCONF, "r");
    if (fp == NULL) {
	debug(78, 1) ("%s: %s\n", _PATH_RESCONF, xstrerror());
	return;
    }
#if defined(_SQUID_WIN32_)
    setmode(fileno(fp), O_TEXT);
#endif
    while (fgets(buf, RESOLV_BUFSZ, fp)) {
	t = strtok(buf, w_space);
	if (NULL == t) {
	    continue;
	} else if (strcasecmp(t, "nameserver") == 0) {
	    t = strtok(NULL, w_space);
	    if (NULL == t)
		continue;
	    debug(78, 1) ("Adding nameserver %s from %s\n", t, _PATH_RESCONF);
	    idnsAddNameserver(t);
	} else if (strcasecmp(t, "domain") == 0) {
	    idnsFreeSearchpath();
	    t = strtok(NULL, w_space);
	    if (NULL == t)
		continue;
	    debug(78, 1) ("Adding domain %s from %s\n", t, _PATH_RESCONF);
	    idnsAddPathComponent(t);
	} else if (strcasecmp(t, "search") == 0) {
	    idnsFreeSearchpath();
	    while (NULL != t) {
		t = strtok(NULL, w_space);
		if (NULL == t)
		    continue;
		debug(78, 1) ("Adding domain %s from %s\n", t, _PATH_RESCONF);
		idnsAddPathComponent(t);
	    }
	} else if (strcasecmp(t, "options") == 0) {
	    while (NULL != t) {
		t = strtok(NULL, w_space);
		if (NULL == t)
		    continue;
		if (strncmp(t, "ndots:", 6) != 0) {
		    DnsConfig.ndots = atoi(t + 6);
		    if (DnsConfig.ndots < 1)
			DnsConfig.ndots = 1;
		    if (DnsConfig.ndots > RES_MAXNDOTS)
			DnsConfig.ndots = RES_MAXNDOTS;
		    debug(78, 1) ("Adding ndots %d from %s\n", DnsConfig.ndots, _PATH_RESCONF);
		}
	    }
	}
    }
    fclose(fp);

    if (npc == 0 && (t = getMyHostname())) {
	t = strchr(t, '.');
	if (t)
	    idnsAddPathComponent(t + 1);
    }
}
#endif

#ifdef _SQUID_WIN32_
static void
idnsParseWIN32SearchList(const char *Separator)
{
    const char *t;
    char *token;
    HKEY hndKey;

    if (RegOpenKey(HKEY_LOCAL_MACHINE,
	    "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
	    &hndKey) == ERROR_SUCCESS) {
	char *t;

	DWORD Type = 0;
	DWORD Size = 0;
	LONG Result;
	Result =
	    RegQueryValueEx(hndKey, "Domain", NULL, &Type, NULL,
	    &Size);

	if (Result == ERROR_SUCCESS && Size) {
	    t = (char *) xmalloc(Size);
	    RegQueryValueEx(hndKey, "Domain", NULL, &Type, (LPBYTE) t,
		&Size);
	    debug(78, 1) ("Adding domain %s from Registry\n", t);
	    idnsAddPathComponent(t);
	    xfree(t);
	}
	Result =
	    RegQueryValueEx(hndKey, "SearchList", NULL, &Type, NULL,
	    &Size);

	if (Result == ERROR_SUCCESS && Size) {
	    t = (char *) xmalloc(Size);
	    RegQueryValueEx(hndKey, "SearchList", NULL, &Type, (LPBYTE) t,
		&Size);
	    token = strtok(t, Separator);
	    idnsFreeSearchpath();

	    while (token) {
		idnsAddPathComponent(token);
		debug(78, 1) ("Adding domain %s from Registry\n", token);
		token = strtok(NULL, Separator);
	    }
	    xfree(t);
	}
	RegCloseKey(hndKey);
    }
    if (npc == 0 && (t = getMyHostname())) {
	t = strchr(t, '.');
	if (t)
	    idnsAddPathComponent(t + 1);
    }
}

static void
idnsParseWIN32Registry(void)
{
    char *t;
    char *token;
    HKEY hndKey, hndKey2;

    switch (WIN32_OS_version) {
    case _WIN_OS_WINNT:
	/* get nameservers from the Windows NT registry */
	if (RegOpenKey(HKEY_LOCAL_MACHINE,
		"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
		&hndKey) == ERROR_SUCCESS) {
	    DWORD Type = 0;
	    DWORD Size = 0;
	    LONG Result;
	    Result =
		RegQueryValueEx(hndKey, "DhcpNameServer", NULL, &Type, NULL,
		&Size);
	    if (Result == ERROR_SUCCESS && Size) {
		t = (char *) xmalloc(Size);
		RegQueryValueEx(hndKey, "DhcpNameServer", NULL, &Type, t,
		    &Size);
		token = strtok(t, ", ");
		while (token) {
		    idnsAddNameserver(token);
		    debug(78, 1) ("Adding DHCP nameserver %s from Registry\n",
			token);
		    token = strtok(NULL, ", ");
		}
		xfree(t);
	    }
	    Result =
		RegQueryValueEx(hndKey, "NameServer", NULL, &Type, NULL, &Size);
	    if (Result == ERROR_SUCCESS && Size) {
		t = (char *) xmalloc(Size);
		RegQueryValueEx(hndKey, "NameServer", NULL, &Type, t, &Size);
		token = strtok(t, ", ");
		while (token) {
		    debug(78, 1) ("Adding nameserver %s from Registry\n",
			token);
		    idnsAddNameserver(token);
		    token = strtok(NULL, ", ");
		}
		xfree(t);
	    }
	    RegCloseKey(hndKey);
	}
	idnsParseWIN32SearchList(" ");
	break;
    case _WIN_OS_WIN2K:
    case _WIN_OS_WINXP:
    case _WIN_OS_WINNET:
    case _WIN_OS_WINLON:
    case _WIN_OS_WIN7:
	/* get nameservers from the Windows 2000 registry */
	/* search all interfaces for DNS server addresses */
	if (RegOpenKey(HKEY_LOCAL_MACHINE,
		"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces",
		&hndKey) == ERROR_SUCCESS) {
	    int i;
	    char keyname[255];

	    for (i = 0; i < 10; i++) {
		if (RegEnumKey(hndKey, i, (char *) &keyname,
			255) == ERROR_SUCCESS) {
		    char newkeyname[255];
		    strcpy(newkeyname,
			"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\");
		    strcat(newkeyname, keyname);
		    if (RegOpenKey(HKEY_LOCAL_MACHINE, newkeyname,
			    &hndKey2) == ERROR_SUCCESS) {
			DWORD Type = 0;
			DWORD Size = 0;
			LONG Result;
			Result =
			    RegQueryValueEx(hndKey2, "DhcpNameServer", NULL,
			    &Type, NULL, &Size);
			if (Result == ERROR_SUCCESS && Size) {
			    t = (char *) xmalloc(Size);
			    RegQueryValueEx(hndKey2, "DhcpNameServer", NULL,
				&Type, t, &Size);
			    token = strtok(t, ", ");
			    while (token) {
				debug(78, 1)
				    ("Adding DHCP nameserver %s from Registry\n",
				    token);
				idnsAddNameserver(token);
				token = strtok(NULL, ", ");
			    }
			    xfree(t);
			}
			Result =
			    RegQueryValueEx(hndKey2, "NameServer", NULL, &Type,
			    NULL, &Size);
			if (Result == ERROR_SUCCESS && Size) {
			    t = (char *) xmalloc(Size);
			    RegQueryValueEx(hndKey2, "NameServer", NULL, &Type,
				t, &Size);
			    token = strtok(t, ", ");
			    while (token) {
				debug(78, 1) ("Adding nameserver %s from Registry\n",
				    token);
				idnsAddNameserver(token);
				token = strtok(NULL, ", ");
			    }
			    xfree(t);
			}
			RegCloseKey(hndKey2);
		    }
		}
	    }
	    RegCloseKey(hndKey);
	}
	idnsParseWIN32SearchList(", ");
	break;
    case _WIN_OS_WIN95:
    case _WIN_OS_WIN98:
    case _WIN_OS_WINME:
	/* get nameservers from the Windows 9X registry */
	if (RegOpenKey(HKEY_LOCAL_MACHINE,
		"SYSTEM\\CurrentControlSet\\Services\\VxD\\MSTCP",
		&hndKey) == ERROR_SUCCESS) {
	    DWORD Type = 0;
	    DWORD Size = 0;
	    LONG Result;
	    Result =
		RegQueryValueEx(hndKey, "NameServer", NULL, &Type, NULL, &Size);
	    if (Result == ERROR_SUCCESS && Size) {
		t = (char *) xmalloc(Size);
		RegQueryValueEx(hndKey, "NameServer", NULL, &Type, t, &Size);
		token = strtok(t, ", ");
		while (token) {
		    debug(78, 1) ("Adding nameserver %s from Registry\n",
			token);
		    idnsAddNameserver(token);
		    token = strtok(NULL, ", ");
		}
		xfree(t);
	    }
	    RegCloseKey(hndKey);
	}
	break;
    default:
	debug(78, 1)
	    ("Failed to read nameserver from Registry: Unknown System Type.\n");
	return;
    }
}
#endif

static void
idnsStats(StoreEntry * sentry)
{
    dlink_node *n;
    idns_query *q;
    int i;
    int j;
    storeAppendPrintf(sentry, "Internal DNS Statistics:\n");
    storeAppendPrintf(sentry, "\nThe Queue:\n");
    storeAppendPrintf(sentry, "                       DELAY SINCE\n");
    storeAppendPrintf(sentry, "  ID   SIZE SENDS FIRST SEND LAST SEND PRO NAME\n");
    storeAppendPrintf(sentry, "------ ---- ----- ---------- --------- --- ------------------\n");
    for (n = idns_lru_list.head; n; n = n->next) {
	q = n->data;
	storeAppendPrintf(sentry, "%#06x %4d %5d %10.3f %9.3f %3s %s\n",
	    (int) q->id, (int) q->sz, q->nsends,
	    tvSubDsec(q->start_t, current_time),
	    tvSubDsec(q->sent_t, current_time),
	    q->tcp_socket != -1 ? "TCP" : "UDP",
	    q->name);
    }
    storeAppendPrintf(sentry, "\nNameservers:\n");
    storeAppendPrintf(sentry, "IP ADDRESS      # QUERIES # REPLIES\n");
    storeAppendPrintf(sentry, "--------------- --------- ---------\n");
    for (i = 0; i < nns; i++) {
	LOCAL_ARRAY(char, sbuf, 256);
	(void) sqinet_ntoa(&nameservers[i].S, sbuf, sizeof(sbuf), SQADDR_NONE);
	storeAppendPrintf(sentry, "%-15s %9d %9d\n",
	    sbuf,
	    nameservers[i].nqueries,
	    nameservers[i].nreplies);
    }
    storeAppendPrintf(sentry, "\nRcode Matrix:\n");
    storeAppendPrintf(sentry, "RCODE");
    for (i = 0; i < MAX_ATTEMPT; i++)
	storeAppendPrintf(sentry, " ATTEMPT%d", i + 1);
    storeAppendPrintf(sentry, "\n");
    for (j = 0; j < MAX_RCODE; j++) {
	storeAppendPrintf(sentry, "%5d", j);
	for (i = 0; i < MAX_ATTEMPT; i++)
	    storeAppendPrintf(sentry, " %8d", RcodeMatrix[j][i]);
	storeAppendPrintf(sentry, "\n");
    }
    if (npc) {
	storeAppendPrintf(sentry, "\nSearch list:\n");
	for (i = 0; i < npc; i++)
	    storeAppendPrintf(sentry, "%s\n", searchpath[i].domain);
	storeAppendPrintf(sentry, "\n");
    }
}

/* ====================================================================== */

void
idnsInternalInit(void)
{
    static int init = 0;

    assert(0 == nns);
    idnsParseNameservers();
#ifndef _SQUID_MSWIN_
    if (0 == nns)
	idnsParseResolvConf();
#endif
#ifdef _SQUID_WIN32_
    if (0 == nns)
	idnsParseWIN32Registry();
#endif
    if (0 == nns) {
	debug(78, 1) ("Warning: Could not find any nameservers. Trying to use localhost\n");
#ifdef _SQUID_WIN32_
	debug(78, 1) ("Please check your TCP-IP settings or /etc/resolv.conf file\n");
#else
	debug(78, 1) ("Please check your /etc/resolv.conf file\n");
#endif
	debug(78, 1) ("or use the 'dns_nameservers' option in squid.conf.\n");
	idnsAddNameserver("127.0.0.1");
    }
    if (!init) {
	cachemgrRegister("idns",
	    "Internal DNS Statistics",
	    idnsStats, 0, 1);
	init++;
    }
}

#ifdef SQUID_SNMP
/*
 * The function to return the DNS via SNMP
 */
variable_list *
snmp_netIdnsFn(variable_list * Var, snint * ErrP)
{
    int i, n = 0;
    variable_list *Answer = NULL;
    debug(49, 5) ("snmp_netIdnsFn: Processing request: \n");
    snmpDebugOid(5, Var->name, Var->name_length);
    *ErrP = SNMP_ERR_NOERROR;
    switch (Var->name[LEN_SQ_NET + 1]) {
    case DNS_REQ:
	for (i = 0; i < nns; i++)
	    n += nameservers[i].nqueries;
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    n,
	    SMI_COUNTER32);
	break;
    case DNS_REP:
	for (i = 0; i < nns; i++)
	    n += nameservers[i].nreplies;
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    n,
	    SMI_COUNTER32);
	break;
    case DNS_SERVERS:
	Answer = snmp_var_new_integer(Var->name, Var->name_length,
	    nns,
	    SMI_COUNTER32);
	break;
    default:
	*ErrP = SNMP_ERR_NOSUCHNAME;
	break;
    }
    return Answer;
}
#endif /* SQUID_SNMP */
