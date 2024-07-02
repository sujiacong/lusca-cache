
/*
 * $Id: HttpHeader.c 12651 2008-04-25 16:47:11Z adrian.chadd $
 *
 * DEBUG: section 55    HTTP Header
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
#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#include <ctype.h>

#include "../include/Array.h"
#include "../include/Stack.h"
#include "../include/Vector.h"
#include "../include/util.h"
#include "../libcore/valgrind.h"
#include "../libcore/varargs.h"
#include "../libcore/debug.h"
#include "../libcore/kb.h"
#include "../libcore/gb.h"
#include "../libcore/tools.h"

#include "../libmem/MemPool.h"
#include "../libmem/MemBufs.h"
#include "../libmem/MemBuf.h"
#include "../libmem/String.h"

#include "../libcb/cbdata.h"

#include "../libstat/StatHist.h"

#include "HttpVersion.h"
#include "HttpStatusLine.h"
#include "HttpHeaderType.h"
#include "HttpHeaderFieldStat.h"
#include "HttpHeaderFieldInfo.h"
#include "HttpHeaderEntry.h"
#include "HttpHeader.h"
#include "HttpHeaderStats.h"
#include "HttpHeaderTools.h"
#include "HttpHeaderParse.h"

int httpConfig_relaxed_parser = 0;
int HeaderEntryParsedCount = 0;

typedef enum {
	PR_NONE,
	PR_ERROR,
	PR_IGNORE,
	PR_WARN,
	PR_OK
} parse_retval_t;

static parse_retval_t httpHeaderEntryParseCreate(HttpHeader *hdr, const char *field_start, const char *field_end);
int httpHeaderParseSize2(const char *start, int len, squid_off_t * value);

/*
 * Check whether the given content length header value is "sensible".
 *
 * Returns 1 on "yes, use"; 0 on "no; don't use but don't error", -1 on "error out."
 *
 * This routine will delete the older version of the header - it shouldn't
 * do that! It should signify the caller that the old header value should
 * be removed!
 */
parse_retval_t
hh_check_content_length(HttpHeader *hdr, const char *var, int vlen)
{
	    squid_off_t l1, l2;
	    HttpHeaderEntry *e2;

	    if (!httpHeaderParseSize2(var, vlen, &l1)) {
		debug(55, 1) ("WARNING: Unparseable content-length '%.*s'\n", vlen, var);
		return PR_ERROR;
	    }
	    e2 = httpHeaderFindEntry(hdr, HDR_CONTENT_LENGTH);

	    /* No header found? We're safe */
	    if (! e2)
                return PR_OK;

	    /* Do the contents match? */
	    if ((vlen == strLen2(e2->value)) &&
		(strNCmp(e2->value, var, vlen) == 0)) {
		debug(55, httpConfig_relaxed_parser <= 0 ? 1 : 2) ("NOTICE: found double content-length header\n");
		if (httpConfig_relaxed_parser) {
		    return PR_IGNORE;
		} else {
		    return PR_ERROR;
		}
            }

            /* We have two conflicting Content-Length headers at this point */
	    debug(55, httpConfig_relaxed_parser <= 0 ? 1 : 2) ("WARNING: found two conflicting content-length headers\n");

	    /* XXX Relaxed parser is off - return an error? */
	    /* XXX what was this before? */
	    if (!httpConfig_relaxed_parser) {
	        return PR_ERROR;
	    }

	    /* Is the original entry parseable? If not, definitely error out. It shouldn't be here */
	    if (!httpHeaderParseSize2(strBuf2(e2->value), strLen2(e2->value), &l2)) {
	        debug(55, 1) ("WARNING: Unparseable content-length '%.*s'\n", vlen, var);
	        return PR_ERROR;
	    }

	    /* Is the new entry larger than the old one? Delete the old one */
	    /* If not, we just don't add the new entry */
	    if (l1 > l2) {
	        httpHeaderDelById(hdr, e2->id);
		return PR_OK;
	    } else {
	        return PR_IGNORE;
	    }
}

int
httpHeaderParse(HttpHeader * hdr, const char *header_start, const char *header_end)
{
    const char *field_ptr = header_start;
    HttpHeaderEntry *e;
    parse_retval_t r;

    assert(hdr);
    assert(header_start && header_end);
    debug(55, 7) ("parsing hdr: (%p)\n%.*s\n", hdr, charBufferSize(header_start, header_end), header_start);
    HttpHeaderStats[hdr->owner].parsedCount++;
    if (memchr(header_start, '\0', header_end - header_start)) {
	debug(55, 1) ("WARNING: HTTP header contains NULL characters {%.*s}\n",
	    charBufferSize(header_start, header_end), header_start);
	return httpHeaderReset(hdr);
    }
    /* common format headers are "<name>:[ws]<value>" lines delimited by <CRLF>.
     * continuation lines start with a (single) space or tab */
    while (field_ptr < header_end) {
	const char *field_start = field_ptr;
	const char *field_end;
	do {
	    const char *this_line = field_ptr;
	    field_ptr = memchr(field_ptr, '\n', header_end - field_ptr);
	    if (!field_ptr)
		return httpHeaderReset(hdr);	/* missing <LF> */
	    field_end = field_ptr;
	    field_ptr++;	/* Move to next line */
	    if (field_end > this_line && field_end[-1] == '\r') {
		field_end--;	/* Ignore CR LF */
		/* Ignore CR CR LF in relaxed mode */
		if (httpConfig_relaxed_parser && field_end > this_line + 1 && field_end[-1] == '\r') {
		    debug(55, httpConfig_relaxed_parser <= 0 ? 1 : 2)
			("WARNING: Double CR characters in HTTP header {%.*s}\n", charBufferSize(field_start, field_end), field_start);
		    field_end--;
		}
	    }
	    /* Barf on stray CR characters */
	    if (memchr(this_line, '\r', field_end - this_line)) {
		debug(55, 1) ("WARNING: suspicious CR characters in HTTP header {%.*s}\n",
		    charBufferSize(field_start, field_end), field_start);
		if (httpConfig_relaxed_parser) {
		    char *p = (char *) this_line;	/* XXX Warning! This destroys original header content and violates specifications somewhat */
		    while ((p = memchr(p, '\r', field_end - p)) != NULL)
			*p++ = ' ';
		} else
		    return httpHeaderReset(hdr);
	    }
	    if (this_line + 1 == field_end && this_line > field_start) {
		debug(55, 1) ("WARNING: Blank continuation line in HTTP header {%.*s}\n",
		    charBufferSize(header_start, header_end), header_start);
		return httpHeaderReset(hdr);
	    }
	} while (field_ptr < header_end && (*field_ptr == ' ' || *field_ptr == '\t'));
	if (field_start == field_end) {
	    if (field_ptr < header_end) {
		debug(55, 1) ("WARNING: unparseable HTTP header field near {%.*s}\n",
		    charBufferSize(field_start, field_end), field_start);
		return httpHeaderReset(hdr);
	    }
	    break;		/* terminating blank line */
	}

	/* This now parses and creates the entry */
	r = httpHeaderEntryParseCreate(hdr, field_start, field_end);
	if (r == PR_ERROR)
	    return httpHeaderReset(hdr);
	else if (r == PR_WARN) {
	    debug(55, 1) ("WARNING: unparseable HTTP header field {%.*s}\n",
		charBufferSize(field_start, field_end), field_start);
	    debug(55, httpConfig_relaxed_parser <= 0 ? 1 : 2)
		(" in {%.*s}\n", charBufferSize(header_start, header_end), header_start);
	    if (httpConfig_relaxed_parser)
		continue;
	    else
		return httpHeaderReset(hdr);
	}
    }
    return 1;			/* even if no fields where found, it is a valid header */
}

/*
 * HttpHeaderEntry
 */

/* parses and inits header entry, returns new entry on success */
parse_retval_t
httpHeaderEntryParseCreate(HttpHeader *hdr, const char *field_start, const char *field_end)
{
    HttpHeaderEntry *e;
    int id;
    parse_retval_t r = PR_OK;

    /* note: name_start == field_start */
    const char *name_end = memchr(field_start, ':', field_end - field_start);
    int name_len = name_end ? name_end - field_start : 0;
    const char *value_start = field_start + name_len + 1;	/* skip ':' */
    /* note: value_end == field_end */

    HeaderEntryParsedCount++;

    /* do we have a valid field name within this field? */
    if (!name_len || name_end > field_end)
	return PR_IGNORE;
    if (name_len > 65534) {
	/* String must be LESS THAN 64K and it adds a terminating NULL */
	debug(55, 1) ("WARNING: ignoring header name of %d bytes\n", name_len);
	return PR_IGNORE;
    }
    if (httpConfig_relaxed_parser && xisspace(field_start[name_len - 1])) {
	debug(55, httpConfig_relaxed_parser <= 0 ? 1 : 2)
	    ("NOTICE: Whitespace after header name in '%.*s'\n", charBufferSize(field_start, field_end), field_start);
	while (name_len > 0 && xisspace(field_start[name_len - 1]))
	    name_len--;
	if (!name_len)
	    return PR_IGNORE;
    }

    /* now we know we can parse it */

    /* is it a "known" field? */
    id = httpHeaderIdByName(field_start, name_len, Headers, HDR_ENUM_END);
    if (id < 0)
	id = HDR_OTHER;
    assert_eid(id);

    /* trim field value */
    while (value_start < field_end && xisspace(*value_start))
	value_start++;
    while (value_start < field_end && xisspace(field_end[-1]))
	field_end--;

    if (field_end - value_start > 65534) {
	/* String must be LESS THAN 64K and it adds a terminating NULL */
	debug(55, 1) ("WARNING: ignoring '%.*s' header of %d bytes\n",
	   charBufferSize(field_start, field_end), field_start,  charBufferSize(value_start, field_end));
	return PR_IGNORE;
    }

    /* Is it an OTHER header? Verify the header contents don't have whitespace! */

    if (id == HDR_OTHER && strpbrk_n(field_start, name_len, w_space)) {
	    debug(55, httpConfig_relaxed_parser <= 0 ? 1 : 2)
		("WARNING: found whitespace in HTTP header name {%.*s}\n", name_len, field_start);
	    if (!httpConfig_relaxed_parser) {
		return PR_IGNORE;
	    }
    }

    /* Is it a content length header? Do the content length checks */
    /* XXX this function will remove the older content length header(s)
     * XXX if needed. Ew. */
    if (id == HDR_CONTENT_LENGTH) {
        r = hh_check_content_length(hdr, value_start, field_end - value_start);
	/* Warn is fine */
        if (r != PR_OK && r != PR_WARN)
	    return r;
    }

    /* Create the entry and return it */
    (void) httpHeaderAddEntryStr2(hdr, id, field_start, name_len, value_start, field_end - value_start);
    return r;
}


/*
 * like httpHeaderParseSize(), but takes a "len" parameter for the length
 * of the string buffer.
 */
int
httpHeaderParseSize2(const char *start, int len, squid_off_t * value)
{
    char *end;
    errno = 0;
    assert(value);

    *value = strtol_n(start, len, &end, 10);
    if (start == end || errno != 0) {
        debug(55, 2) ("httpHeaderParseSize2: failed to parse a size/offset header field near '%s'\n", start);
        *value = -1;
        return 0;
    }
    return 1;
}


/*
 * parses an int field, complains if soemthing went wrong, returns true on
 * success
 */
int
httpHeaderParseInt(const char *start, int *value)
{
    char *end;
    long v;
    assert(value);
    errno = 0;
    v = *value = strtol(start, &end, 10);
    if (start == end || errno != 0 || v != *value) {
        debug(66, 2) ("failed to parse an int header field near '%s'\n", start);
        *value = -1;
        return 0;
    }
    return 1;
}

int
httpHeaderParseSize(const char *start, squid_off_t * value)
{
    char *end;
    errno = 0;
    assert(value);
    *value = strto_off_t(start, &end, 10);
    if (start == end || errno != 0) {
        debug(66, 2) ("failed to parse a size/offset header field near '%s'\n", start);
        *value = -1;
        return 0;
    }
    return 1;
}

void
httpHeaderNoteParsedEntry(http_hdr_type id, String context, int error)
{
    Headers[id].stat.parsCount++;
    if (error) {
        Headers[id].stat.errCount++;
        debug(55, 2) ("cannot parse hdr field: '%.*s: %.*s'\n",
            strLen2(Headers[id].name),  strBuf2(Headers[id].name), strLen2(context), strBuf2(context));
    }
}

