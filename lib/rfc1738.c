/*
 * $Id: rfc1738.c 14364 2009-11-05 04:56:18Z adrian.chadd $
 *
 * DEBUG: 
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

#include "config.h"

#if HAVE_STDIO_H
#include <stdio.h>
#endif
#if HAVE_STRING_H
#include <string.h>
#endif

#include "util.h"

static char rfc1738_unsafe_char_map[] =
{
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
};

static char rfc1738_reserved_char_map[] =
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
};

#if 0
/*  
 *  RFC 1738 defines that these characters should be escaped, as well
 *  any non-US-ASCII character and anything between 0x00 and 0x1F.
 */
static char rfc1738_unsafe_chars[] =
{
    (char) 0x3C,		/* < */
    (char) 0x3E,		/* > */
    (char) 0x22,		/* " */
    (char) 0x23,		/* # */
#if 0				/* done in code */
    (char) 0x25,		/* % */
#endif
    (char) 0x7B,		/* { */
    (char) 0x7D,		/* } */
    (char) 0x7C,		/* | */
    (char) 0x5C,		/* \ */
    (char) 0x5E,		/* ^ */
    (char) 0x7E,		/* ~ */
    (char) 0x5B,		/* [ */
    (char) 0x5D,		/* ] */
    (char) 0x60,		/* ` */
    (char) 0x27,		/* ' */
    (char) 0x20			/* space */
};

static char rfc1738_reserved_chars[] =
{
    (char) 0x3b,		/* ; */
    (char) 0x2f,		/* / */
    (char) 0x3f,		/* ? */
    (char) 0x3a,		/* : */
    (char) 0x40,		/* @ */
    (char) 0x3d,		/* = */
    (char) 0x26			/* & */
};
#endif

/*
 *  rfc1738_escape - Returns a static buffer contains the RFC 1738 
 *  compliant, escaped version of the given url.
 */
static char *
rfc1738_do_escape(const char *url, int encode_reserved)
{
    static char *buf;
    static size_t bufsize = 0;
    const char *p;
    char *q;
    unsigned int do_escape;

    if (buf == NULL || strlen(url) * 3 > bufsize) {
	xfree(buf);
	bufsize = strlen(url) * 3 + 1;
	buf = xcalloc(bufsize, 1);
    }
    for (p = url, q = buf; *p != '\0'; p++, q++) {
	do_escape = 0;

        /* Handle unsafe characters */
	if (rfc1738_unsafe_char_map[(int) *p] > 0)
	    do_escape = 1;

	/* Handle % separately */
	else if (encode_reserved >= 0 && *p == '%')
	    do_escape = 1;

        /* Handle reserved characters */
	else if (encode_reserved > 0 && rfc1738_reserved_char_map[ (int) *p ] > 0)
		do_escape = 1;

	/* Do the triplet encoding, or just copy the char */
	/* note: we do not need snprintf here as q is appropriately
	 * allocated - KA */

	if (do_escape == 1) {
	    (void) sprintf(q, "%%%02X", (unsigned char) *p);
	    q += sizeof(char) * 2;
	} else {
	    *q = *p;
	}
    }
    *q = '\0';
    return (buf);
}

/*
 * rfc1738_escape - Returns a static buffer that contains the RFC
 * 1738 compliant, escaped version of the given url.
 */
char *
rfc1738_escape(const char *url)
{
    return rfc1738_do_escape(url, 0);
}

/*
 * rfc1738_escape_unescaped - Returns a static buffer that contains
 * the RFC 1738 compliant, escaped version of the given url.
 */
char *
rfc1738_escape_unescaped(const char *url)
{
    return rfc1738_do_escape(url, -1);
}

/*
 * rfc1738_escape_part - Returns a static buffer that contains the
 * RFC 1738 compliant, escaped version of the given url segment.
 */
char *
rfc1738_escape_part(const char *url)
{
    return rfc1738_do_escape(url, 1);
}

/*
 *  rfc1738_unescape() - Converts escaped characters (%xy numbers) in 
 *  given the string.  %% is a %. %ab is the 8-bit hexadecimal number "ab"
 */
static inline int
fromhex(char ch)
{
    if (ch >= '0' && ch <= '9')
	return ch - '0';
    if (ch >= 'a' && ch <= 'f')
	return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
	return ch - 'A' + 10;
    return -1;
}

void
rfc1738_unescape(char *s_)
{
    unsigned char *s = (unsigned char *) s_;
    int i, j;			/* i is write, j is read */
    for (i = j = 0; s[j]; i++, j++) {
	s[i] = s[j];
	if (s[j] != '%') {
	    /* normal case, nothing more to do */
	} else if (s[j + 1] == '%') {	/* %% case */
	    j++;		/* Skip % */
	} else {
	    /* decode */
	    char v1, v2;
	    int x;
	    v1 = fromhex(s[j + 1]);
	    v2 = fromhex(s[j + 2]);
	    /* fromhex returns -1 on error which brings this out of range (|, not +) */
	    x = v1 << 4 | v2;
	    if (x > 0 && x <= 255) {
		s[i] = x;
		j += 2;
	    }
	}
    }
    s[i] = '\0';
}
