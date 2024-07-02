
/*
 * $Id: event.c 13767 2009-02-01 23:02:44Z adrian.chadd $
 *
 * DEBUG: section 41    Event Processing
 * AUTHOR: Henrik Nordstrom
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

static OBJH eventMgr;
static OBJH eventDump;

void
eventLocalInit(void)
{
    cachemgrRegister("events",
	"Event Queue",
	eventMgr, 0, 1);
}

static void
eventDump(StoreEntry * sentry)
{
    struct ev_entry *e = tasks;
    if (last_event_ran)
	storeAppendPrintf(sentry, "Last event to run: %s\n\n", last_event_ran);
    storeAppendPrintf(sentry, "%s\t%s\t%s\t%s\n",
	"Operation",
	"Next Execution",
	"Weight",
	"Callback Valid?");
    while (e != NULL) {
	storeAppendPrintf(sentry, "%s\t%f seconds\t%d\t%s\n",
	    e->name, e->when - current_dtime, e->weight,
	    e->arg ? cbdataValid(e->arg) ? "yes" : "no" : "N/A");
	e = e->next;
    }
}

static void
eventMgr(StoreEntry * e)
{
    eventDump(e);
}
