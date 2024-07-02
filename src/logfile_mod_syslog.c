/*
 * $Id: logfile_mod_syslog.c 12937 2008-06-26 12:09:24Z adrian.chadd $
 *
 * DEBUG: section 50    Log file handling
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
#if HAVE_SYSLOG
#include "logfile_mod_syslog.h"

typedef struct {
    int syslog_priority;
} l_syslog_t;

static void
logfile_mod_syslog_writeline(Logfile * lf, const char *buf, size_t len)
{
    l_syslog_t *ll = (l_syslog_t *) lf->data;
    syslog(ll->syslog_priority, "%s", (char *) buf);
}

static void
logfile_mod_syslog_linestart(Logfile * lf)
{
}

static void
logfile_mod_syslog_lineend(Logfile * lf)
{
}

static void
logfile_mod_syslog_flush(Logfile * lf)
{
}

static void
logfile_mod_syslog_rotate(Logfile * lf)
{
}

static void
logfile_mod_syslog_close(Logfile * lf)
{
    xfree(lf->data);
    lf->data = NULL;
}



/*
 * This code expects the path to be syslog:<priority>
 */
int
logfile_mod_syslog_open(Logfile * lf, const char *path, size_t bufsz, int fatal_flag)
{
    l_syslog_t *ll;

    lf->f_close = logfile_mod_syslog_close;
    lf->f_linewrite = logfile_mod_syslog_writeline;
    lf->f_linestart = logfile_mod_syslog_linestart;
    lf->f_lineend = logfile_mod_syslog_lineend;
    lf->f_flush = logfile_mod_syslog_flush;
    lf->f_rotate = logfile_mod_syslog_rotate;

    ll = xcalloc(1, sizeof(*ll));
    lf->data = ll;

    if (*path) {
	char *priority = xstrdup(path);
	char *facility = (char *) strchr(priority, '.');
	if (!facility)
	    facility = (char *) strchr(priority, '|');
	if (facility) {
	    *facility++ = '\0';
	    ll->syslog_priority |= syslog_ntoa(facility);
	}
	ll->syslog_priority |= syslog_ntoa(priority);
	xfree(priority);
    }
    if ((ll->syslog_priority & PRIORITY_MASK) == 0)
	ll->syslog_priority |= LOG_INFO;

    return 1;
}
#endif
