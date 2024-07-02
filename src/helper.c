
/*
 * $Id: helper.c 13022 2008-07-05 12:03:46Z adrian.chadd $
 *
 * DEBUG: section 84    Helper process maintenance
 * AUTHOR: Harvest Derived?
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

void
helperStats(StoreEntry * sentry, helper * hlp)
{
    dlink_node *link;
    storeAppendPrintf(sentry, "program: %s\n",
	hlp->cmdline->key);
    storeAppendPrintf(sentry, "number running: %d of %d\n",
	hlp->n_running, hlp->n_to_start);
    storeAppendPrintf(sentry, "requests sent: %d\n",
	hlp->stats.requests);
    storeAppendPrintf(sentry, "replies received: %d\n",
	hlp->stats.replies);
    storeAppendPrintf(sentry, "queue length: %d\n",
	hlp->stats.queue_size);
    storeAppendPrintf(sentry, "avg service time: %.2f msec\n",
	(double) hlp->stats.avg_svc_time / 1000.0);
    storeAppendPrintf(sentry, "\n");
    storeAppendPrintf(sentry, "%7s\t%7s\t%7s\t%11s\t%9s\t%s\t%7s\t%7s\t%7s\n",
	"#",
	"FD",
	"PID",
	"# Requests",
	"# Pending",
	"Flags",
	"Time",
	"Offset",
	"Request");
    for (link = hlp->servers.head; link; link = link->next) {
	helper_server *srv = link->data;
	double tt = srv->requests[0] ? 0.001 *
	tvSubMsec(srv->requests[0]->dispatch_time, current_time) : 0.0;
	storeAppendPrintf(sentry, "%7d\t%7d\t%7d\t%11d\t%9d\t%c%c%c\t%7.3f\t%7d\t%s\n",
	    srv->index + 1,
	    srv->rfd,
	    srv->pid,
	    srv->stats.uses,
	    srv->stats.pending,
	    srv->stats.pending ? 'B' : ' ',
	    srv->flags.closing ? 'C' : ' ',
	    srv->flags.shutdown ? 'S' : ' ',
	    tt < 0.0 ? 0.0 : tt,
	    srv->roffset,
	    srv->requests[0] ? log_quote(srv->requests[0]->buf) : "(none)");
    }
    storeAppendPrintf(sentry, "\nFlags key:\n\n");
    storeAppendPrintf(sentry, "   B = BUSY\n");
    storeAppendPrintf(sentry, "   C = CLOSING\n");
    storeAppendPrintf(sentry, "   S = SHUTDOWN\n");
}

void
helperStatefulStats(StoreEntry * sentry, statefulhelper * hlp)
{
    helper_stateful_server *srv;
    dlink_node *link;
    double tt;
    storeAppendPrintf(sentry, "program: %s\n",
	hlp->cmdline->key);
    storeAppendPrintf(sentry, "number running: %d of %d\n",
	hlp->n_running, hlp->n_to_start);
    storeAppendPrintf(sentry, "requests sent: %d\n",
	hlp->stats.requests);
    storeAppendPrintf(sentry, "replies received: %d\n",
	hlp->stats.replies);
    storeAppendPrintf(sentry, "queue length: %d\n",
	hlp->stats.queue_size);
    storeAppendPrintf(sentry, "avg service time: %.2f msec\n",
	(double) hlp->stats.avg_svc_time / 1000.0);
    storeAppendPrintf(sentry, "\n");
    storeAppendPrintf(sentry, "%7s\t%7s\t%7s\t%11s\t%s\t%7s\t%7s\t%7s\n",
	"#",
	"FD",
	"PID",
	"# Requests",
	"Flags",
	"Time",
	"Offset",
	"Request");
    for (link = hlp->servers.head; link; link = link->next) {
	srv = link->data;
	tt = 0.001 * tvSubMsec(srv->dispatch_time,
	    srv->flags.busy ? current_time : srv->answer_time);
	storeAppendPrintf(sentry, "%7d\t%7d\t%7d\t%11d\t%c%c%c%c\t%7.3f\t%7d\t%s\n",
	    srv->index + 1,
	    srv->rfd,
	    srv->pid,
	    srv->stats.uses,
	    srv->flags.busy ? 'B' : ' ',
	    srv->flags.closing ? 'C' : ' ',
	    srv->flags.reserved ? 'R' : ' ',
	    srv->flags.shutdown ? 'S' : ' ',
	    tt < 0.0 ? 0.0 : tt,
	    srv->offset,
	    srv->request ? log_quote(srv->request->buf) : "(none)");
    }
    storeAppendPrintf(sentry, "\nFlags key:\n\n");
    storeAppendPrintf(sentry, "   B = BUSY\n");
    storeAppendPrintf(sentry, "   C = CLOSING\n");
    storeAppendPrintf(sentry, "   R = RESERVED or DEFERRED\n");
    storeAppendPrintf(sentry, "   S = SHUTDOWN\n");
}
