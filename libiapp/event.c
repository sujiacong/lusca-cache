
/*
 * $Id: event.c 14519 2010-03-31 05:32:05Z adrian.chadd $
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

#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include "../include/Array.h"
#include "../include/Stack.h"
#if !HAVE_DRAND48
#include "../include/drand48.h"
#endif

#include "../libcore/valgrind.h"
#include "../libcore/varargs.h"
#include "../libcore/debug.h"
#include "../libcore/kb.h"
#include "../libcore/gb.h"

#include "../libmem/MemPool.h"
#include "../libmem/MemBufs.h"
#include "../libmem/MemBuf.h"

#include "../libcb/cbdata.h"

#include "event.h"


struct ev_entry *tasks = NULL;
static int run_id = 0;
const char *last_event_ran = NULL;
static MemPool * pool_event = NULL;

/* Temporary - the time tracking stuff is still in src/ for now */
extern double current_dtime;

void
eventAdd(const char *name, EVH * func, void *arg, double when, int weight)
{
    struct ev_entry *event = memPoolAlloc(pool_event);
    struct ev_entry **E;
    event->func = func;
    event->arg = arg;
    event->name = name;
    event->when = current_dtime + when;
    event->weight = weight;
    event->id = run_id;
    if (NULL != arg)
	cbdataLock(arg);
    debug(41, 7) ("eventAdd: Adding '%s', in %f seconds\n", name, when);
    /* Insert after the last event with the same or earlier time */
    for (E = &tasks; *E; E = &(*E)->next) {
	if ((*E)->when > event->when)
	    break;
    }
    event->next = *E;
    *E = event;
}

/* same as eventAdd but adds a random offset within +-1/3 of delta_ish */
void
eventAddIsh(const char *name, EVH * func, void *arg, double delta_ish, int weight)
{
    if (delta_ish >= 3.0) {
	const double two_third = (2.0 * delta_ish) / 3.0;
	delta_ish = two_third + (drand48() * two_third);
	/*
	 * I'm sure drand48() isn't portable.  Tell me what function
	 * you have that returns a random double value in the range 0,1.
	 */
    }
    eventAdd(name, func, arg, delta_ish, weight);
}

void
eventDelete(EVH * func, void *arg)
{
    struct ev_entry **E;
    struct ev_entry *event;
    for (E = &tasks; (event = *E) != NULL; E = &(*E)->next) {
	if (event->func != func)
	    continue;
	if (arg && event->arg != arg)
	    continue;
	*E = event->next;
	if (NULL != event->arg)
	    cbdataUnlock(event->arg);
	memPoolFree(pool_event, event);
	return;
    }
    /* We shouldn't get here if the event had an argument! */
    assert(arg == NULL);
}

void
eventRun(void)
{
    struct ev_entry *event = NULL;
    EVH *func;
    void *arg;
    int weight = 0;
    if (NULL == tasks)
	return;
    if (tasks->when > current_dtime)
	return;
    run_id++;
    debug(41, 5) ("eventRun: RUN ID %d\n", run_id);
    while ((event = tasks)) {
	int valid = 1;
	if (event->when > current_dtime)
	    break;
	if (event->id == run_id)	/* was added during this run */
	    break;
	if (weight)
	    break;
	func = event->func;
	arg = event->arg;
	event->func = NULL;
	event->arg = NULL;
	tasks = event->next;
	if (NULL != arg) {
	    valid = cbdataValid(arg);
	    cbdataUnlock(arg);
	}
	if (valid) {
	    weight += event->weight;
	    /* XXX assumes ->name is static memory! */
	    last_event_ran = event->name;
	    debug(41, 5) ("eventRun: Running '%s', id %d\n",
		event->name, event->id);
	    func(arg);
	}
	memPoolFree(pool_event, event);
    }
}

void
eventCleanup(void)
{
    struct ev_entry **p = &tasks;

    debug(41, 2) ("eventCleanup\n");

    while (*p) {
	struct ev_entry *event = *p;
	if (!cbdataValid(event->arg)) {
	    debug(41, 2) ("eventCleanup: cleaning '%s'\n", event->name);
	    *p = event->next;
	    cbdataUnlock(event->arg);
	    memPoolFree(pool_event, event);
	} else {
	    p = &event->next;
	}
    }
}

int
eventNextTime(void)
{
    if (!tasks)
	return 10000;
    return ceil((tasks->when - current_dtime) * 1000);
}

void
eventInit(void)
{
    pool_event = memPoolCreate("event", sizeof(struct ev_entry));
}

void
eventFreeMemory(void)
{
    struct ev_entry *event;
    while ((event = tasks)) {
	tasks = event->next;
	if (NULL != event->arg)
	    cbdataUnlock(event->arg);
	memPoolFree(pool_event, event);
    }
    tasks = NULL;
}

int
eventFind(EVH * func, void *arg)
{
    struct ev_entry *event;
    for (event = tasks; event != NULL; event = event->next) {
	if (event->func == func && event->arg == arg)
	    return 1;
    }
    return 0;
}
