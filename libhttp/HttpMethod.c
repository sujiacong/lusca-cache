
/*
 * $Id: url.c 14346 2009-10-28 12:23:15Z adrian.chadd $
 *
 * DEBUG: section 23    URL Parsing
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

#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#if HAVE_STRING_H
#include <string.h>
#endif

#include "../include/util.h"

#include "../libcore/varargs.h"
#include "../libcore/debug.h"

#include "HttpMethod.h"

struct rms {
    method_t method;
    int string_len;
};

/*
 * It is currently VERY, VERY IMPORTANT that these be in order of their
 * definition in the method_code_t enum.
 */
static struct rms request_methods[] =
{
    {
	{METHOD_NONE, "NONE",
	    {0, 0}}, 4},
    {
	{METHOD_GET, "GET",
	    {1, 0}}, 3},
    {
	{METHOD_POST, "POST",
	    {0, 1}}, 4},
    {
	{METHOD_PUT, "PUT",
	    {0, 1}}, 3},
    {
	{METHOD_HEAD, "HEAD",
	    {1, 0}}, 4},
    {
	{METHOD_CONNECT, "CONNECT",
	    {0, 0}}, 7},
    {
	{METHOD_TRACE, "TRACE",
	    {0, 0}}, 5},
    {
	{METHOD_PURGE, "PURGE",
	    {0, 1}}, 5},
    {
	{METHOD_OPTIONS, "OPTIONS",
	    {0, 0}}, 7},
    {
	{METHOD_DELETE, "DELETE",
	    {0, 1}}, 6},
    {
	{METHOD_PROPFIND, "PROPFIND",
	    {0, 0}}, 8},
    {
	{METHOD_PROPPATCH, "PROPPATCH",
	    {0, 1}}, 9},
    {
	{METHOD_MKCOL, "MKCOL",
	    {0, 1}}, 5},
    {
	{METHOD_COPY, "COPY",
	    {0, 0}}, 4},
    {
	{METHOD_MOVE, "MOVE",
	    {0, 1}}, 4},
    {
	{METHOD_LOCK, "LOCK",
	    {0, 0}}, 4},
    {
	{METHOD_UNLOCK, "UNLOCK",
	    {0, 0}}, 6},
    {
	{METHOD_BMOVE, "BMOVE",
	    {0, 1}}, 5},
    {
	{METHOD_BDELETE, "BDELETE",
	    {0, 1}}, 7},
    {
	{METHOD_BPROPFIND, "BPROPFIND",
	    {0, 0}}, 9},
    {
	{METHOD_BPROPPATCH, "BPROPPATCH",
	    {0, 0}}, 10},
    {
	{METHOD_BCOPY, "BCOPY",
	    {0, 0}}, 5},
    {
	{METHOD_SEARCH, "SEARCH",
	    {0, 0}}, 6},
    {
	{METHOD_SUBSCRIBE, "SUBSCRIBE",
	    {0, 0}}, 9},
    {
	{METHOD_UNSUBSCRIBE, "UNSUBSCRIBE",
	    {0, 0}}, 11},
    {
	{METHOD_POLL, "POLL",
	    {0, 0}}, 4},
    {
	{METHOD_REPORT, "REPORT",
	    {0, 0}}, 6},
    {
	{METHOD_MKACTIVITY, "MKACTIVITY",
	    {0, 0}}, 10},
    {
	{METHOD_CHECKOUT, "CHECKOUT",
	    {0, 0}}, 8},
    {
	{METHOD_MERGE, "MERGE",
	    {0, 0}}, 5},
    {
	{METHOD_OTHER, NULL,
	    {0, 0}}, 0},
};

/*
 * Assign "src" to "dst".
 *
 * This will just copy the pointers for now and log a message
 * whenever a destination pointer is being overwritten that
 * is METHOD_OTHER.
 *      
 * It'll eventually be a "copy" function which will free the
 * original pointer and then dup the origin if it's METHOD_OTHER.
 */ 
void
urlMethodAssign(method_t **dst, method_t *src)
{   
        if (*dst && (*dst)->code == METHOD_OTHER) {
		debugs(23, 1, "urlMethodAssign: overwriting an existing method: '%s'",
		    urlMethodGetConstStr((*dst)));
	}
	if (*dst)
	    urlMethodFree(*dst);

	(*dst) = urlMethodDup(src);
}

method_t *
urlMethodGetKnown(const char *s, int len)
{
    struct rms *rms;

    for (rms = request_methods; rms->string_len != 0; rms++) {
	if (len != rms->string_len) {
	    continue;
	}
	if (strncasecmp(s, rms->method.string, len) == 0) {
	    return (&rms->method);
	}
    }

    return (NULL);
}

method_t *
urlMethodGet(const char *s, int len)
{
    method_t *method;

    method = urlMethodGetKnown(s, len);
    if (method != NULL) {
	return (method);
    }
    method = xmalloc(sizeof(method_t));
    method->code = METHOD_OTHER;
    method->string = xstrndup(s, len + 1);
    method->flags.cachable = 0;
    method->flags.purges_all = 1;

    return (method);
}

method_t *
urlMethodGetKnownByCode(method_code_t code)
{
    if (code < 0 || code >= METHOD_OTHER) {
	return (NULL);
    }
    return (&request_methods[code].method);
}

method_t *
urlMethodDup(method_t * orig)
{
    method_t *method;

    if (orig == NULL) {
	return (NULL);
    }
    if (orig->code != METHOD_OTHER) {
	return (orig);
    }
    method = xmalloc(sizeof(method_t));
    method->code = orig->code;
    method->string = xstrdup(orig->string);
    method->flags.cachable = orig->flags.cachable;
    method->flags.purges_all = orig->flags.purges_all;

    return (method);
}

/*
 * return the string for the method name
 */
const char *
urlMethodGetConstStr(method_t *method)
{
	/* XXX this should log a NULL method! */
	if (! method)
		return "NULL";
	if (! method->string)
		return "NULL";
	return method->string;
}

void
urlMethodFree(method_t * method)
{

    if (method == NULL) {
	return;
    }
    if (method->code != METHOD_OTHER) {
	return;
    }
    xfree((char *) method->string);
    xfree(method);
}

