
/*
 * $Id: HttpHdrRange.c 13448 2008-12-27 00:57:12Z adrian.chadd $
 *
 * DEBUG: section 64    HTTP Range Header
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

static void
httpHdrRangeSpecPackInto(const HttpHdrRangeSpec * spec, Packer * p)
{
    if (!known_spec(spec->offset))	/* suffix */
	packerPrintf(p, "-%" PRINTF_OFF_T, spec->length);
    else if (!known_spec(spec->length))		/* trailer */
	packerPrintf(p, "%" PRINTF_OFF_T "-", spec->offset);
    else			/* range */
	packerPrintf(p, "%" PRINTF_OFF_T "-%" PRINTF_OFF_T,
	    spec->offset, spec->offset + spec->length - 1);
}

void
httpHdrRangePackInto(const HttpHdrRange * range, Packer * p)
{
    HttpHdrRangePos pos = HttpHdrRangeInitPos;
    const HttpHdrRangeSpec *spec;
    assert(range);
    while ((spec = httpHdrRangeGetSpec(range, &pos))) {
	if (pos != HttpHdrRangeInitPos)
	    packerAppend(p, ",", 1);
	httpHdrRangeSpecPackInto(spec, p);
    }
}

/* generates a "unique" boundary string for multipart responses
 * the caller is responsible for cleaning the string */
String
httpHdrRangeBoundaryStr(clientHttpRequest * http)
{
    const char *key;
    String b = StringNull;
    assert(http);
    stringAppend(&b, full_appname_string, strlen(full_appname_string));
    stringAppend(&b, ":", 1);
    key = storeKeyText(http->entry->hash.key);
    stringAppend(&b, key, strlen(key));
    return b;
}

