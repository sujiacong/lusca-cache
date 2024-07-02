
/*
 * $Id: HttpHdrContRange.c 13448 2008-12-27 00:57:12Z adrian.chadd $
 *
 * DEBUG: section 68    HTTP Content-Range Header
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

/*
 *    Currently only byte ranges are supported
 *
 *    Content-Range = "Content-Range" ":" content-range-spec
 *    content-range-spec      = byte-content-range-spec
 *    byte-content-range-spec = bytes-unit SP
 *                              ( byte-range-resp-spec | "*") "/"
 *                              ( entity-length | "*" )
 *    byte-range-resp-spec = first-byte-pos "-" last-byte-pos
 *    entity-length        = 1*DIGIT
 */


static void
httpHdrRangeRespSpecPackInto(const HttpHdrRangeSpec * spec, Packer * p)
{
    if (!known_spec(spec->offset) || !known_spec(spec->length))
	packerPrintf(p, "*");
    else
	packerPrintf(p, "bytes %" PRINTF_OFF_T "-%" PRINTF_OFF_T "",
	    spec->offset, spec->offset + spec->length - 1);
}

void
httpHdrContRangePackInto(const HttpHdrContRange * range, Packer * p)
{
    assert(range && p);
    httpHdrRangeRespSpecPackInto(&range->spec, p);
    if (!known_spec(range->elength))
	packerPrintf(p, "/*");
    else
	packerPrintf(p, "/%ld", (long int) range->elength);
}
