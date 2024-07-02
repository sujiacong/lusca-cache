
/*
 * $Id: debug_syslog.c 14570 2010-04-11 01:08:58Z adrian.chadd $
 *
 * DEBUG: section 0     Debug Routines
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

#include "../include/config.h"

#if HAVE_SYSLOG

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <syslog.h>
        
#include "../libcore/varargs.h"
#include "../libcore/tools.h"
#include "../libcore/syslog_ntoa.h"

#include "debug.h"
#include "debug_syslog.h"

int opt_syslog_enable = 0;
int syslog_facility = LOG_LOCAL4;

void
_db_print_syslog(const char *format, va_list args)
{
    LOCAL_ARRAY(char, tmpbuf, BUFSIZ);
    /* level 0,1 go to syslog */
    if (_db_level > 1)
	return;
    if (0 == opt_syslog_enable)
	return;
    tmpbuf[0] = '\0';
    vsnprintf(tmpbuf, BUFSIZ, format, args);
    tmpbuf[BUFSIZ - 1] = '\0';
    syslog((_db_level == 0 ? LOG_WARNING : LOG_NOTICE) | syslog_facility, "%s", tmpbuf);
}

void
_db_set_syslog(const char *facility)
{
    opt_syslog_enable = 1;
#ifdef LOG_LOCAL4
#ifdef LOG_DAEMON
    syslog_facility = LOG_DAEMON;
#else
    syslog_facility = LOG_LOCAL4;
#endif
    if (facility) {
        syslog_facility = syslog_ntoa(facility);
        if (syslog_facility != 0)
	    return;
         
	fprintf(stderr, "unknown syslog facility '%s'\n", facility);
	exit(1);
    }
#else
    if (facility)
	fprintf(stderr, "syslog facility type not supported on your system\n");
#endif
}

#endif	/* HAVE_SYSLOG */
