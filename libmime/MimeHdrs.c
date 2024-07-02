
/*
 * $Id: mime.c 12731 2008-05-06 15:15:30Z adrian.chadd $
 *
 * DEBUG: section 25    MIME Parsing
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "MimeHdrs.h"

/*
 * @function
 *	headersEnd
 * @abstract
 *	Find the end of the MIME (HTTP?) headers, if any, and return the length.
 * @param	mime		start of headers (not the request/reply)
 * @param	l		size of the headers
 * @return	The length of the headers, or 0 if the headers were incomplete.
 *
 * @discussion
 *	This function pre-supposes that the headers exist - ie, they at the very
 *	least "empty". Calling this for requests with no headers (as seperate
 *	to "empty headers") - eg HTTP/0.9 requests - will simply confuse matters.
 *
 *	This routine was once the sole biggest CPU user at high request rates,
 *	somehow the combination of inefficiently complication and
 * 	inefficient use. The latter has been mostly fixed; the remaining uses
 *	of this function should be eliminated and replaced with just better
 *	code design.
 * 
 */
size_t
headersEnd(const char *mime, size_t l)
{
    size_t e = 0;
    int state = 1;
    while (e < l && state < 3) {
	switch (state) {
	case 0:
	    if ('\n' == mime[e])
		state = 1;
	    break;
	case 1:
	    if ('\r' == mime[e])
		state = 2;
	    else if ('\n' == mime[e])
		state = 3;
	    else
		state = 0;
	    break;
	case 2:
	    if ('\n' == mime[e])
		state = 3;
	    else
		state = 0;
	    break;
	default:
	    break;
	}
	e++;
    }
    if (3 == state)
	return e;
    return 0;
}
