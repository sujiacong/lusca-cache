
/*
 * $Id: HttpStatusLine.c 12824 2008-06-08 02:57:32Z adrian.chadd $
 *
 * DEBUG: section 57    HTTP Status-line
 * AUTHOR: Alex Rousskov
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
httpStatusLinePackInto(const HttpStatusLine * sline, Packer * p)
{
    assert(sline && p);
    debug(57, 9) ("packing sline %p using %p:\n", sline, p);
    debug(57, 9) (HttpStatusLineFormat, sline->version.major,
	sline->version.minor, sline->status,
	sline->reason ? sline->reason : httpStatusString(sline->status));
    packerPrintf(p, HttpStatusLineFormat, sline->version.major,
	sline->version.minor, sline->status, httpStatusLineReason(sline));
}

