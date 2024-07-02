
/*
 * $Id: String.c 10771 2006-05-20 21:39:39Z hno $
 *
 * DEBUG: section 67    String
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
#include <sys/types.h>
#include <string.h>

#include "../include/util.h"
#include "../include/Stack.h"
#include "../libcore/valgrind.h"
#include "../libcore/gb.h"
#include "../libcore/varargs.h" /* required for tools.h */
#include "../libcore/tools.h"
#include "../libcore/debug.h"
  
#include "MemPool.h"
#include "MemStr.h"
#include "String.h"

const String StringNull = { 0, 0, NULL };

static void
stringInitBuf(String * s, size_t sz)
{
    s->buf = memAllocString(sz, &sz);
    assert(sz < 65536);
    s->size = sz;
}

void
stringInit(String * s, const char *str)
{
    assert(s);
    if (str)
	stringLimitInit(s, str, strlen(str));
    else
	*s = StringNull;
}

void
stringLimitInit(String * s, const char *str, int len)
{
    assert(s && str);
    stringInitBuf(s, len + 1);
    s->len = len;
    xmemcpy(s->buf, str, len);
    s->buf[len] = '\0';
}

String
stringDup(const String * s)
{
    String dup;
    assert(s);
    stringLimitInit(&dup, s->buf, s->len);
    return dup;
}

void
stringClean(String * s)
{
    assert(s);
    if (s->buf)
	memFreeString(s->size, s->buf);
    *s = StringNull;
}

void
stringReset(String * s, const char *str)
{
    stringClean(s);
    stringInit(s, str);
}

void
stringAppend(String * s, const char *str, int len)
{
    assert(s);
    assert(str && len >= 0);
    if (s->len + len < s->size) {
	strncat(s->buf, str, len);
	s->len += len;
    } else {
	String snew = StringNull;
	snew.len = s->len + len;
	stringInitBuf(&snew, snew.len + 1);
	if (s->buf)
	    xmemcpy(snew.buf, s->buf, s->len);
	if (len)
	    xmemcpy(snew.buf + s->len, str, len);
	snew.buf[snew.len] = '\0';
	stringClean(s);
	*s = snew;
    }
}

/*
 * This routine SHOULD REQUIRE the string to be something and not NULL
 * but plenty of code unfortunately doesn't check whether the string
 * was empty in the first place.
 *
 * New code MUST NOT call this on an unset string.
 *
 * This copies -from- offset to the end of the string.
 */
char *
stringDupToCOffset(const String *s, int offset)
{
	char *d;

	/* This is horribly temporary [ahc] */
	if (s->buf == NULL)
		return NULL;

	assert(offset <= s->len);
	d = xmalloc(s->len + 1 - offset);
	memcpy(d, s->buf + offset, s->len - offset);
	d[s->len - offset] = '\0';
	return d;
}

char *
stringDupToC(const String *s)
{
	return stringDupToCOffset(s, 0);
}

char *
stringDupSubstrToC(const String *s, int len)
{
	char *d;
	int l = XMIN(len, s->len);

	/* This is horribly temporary [ahc] */
	if (s->buf == NULL)
		return NULL;

	assert(len <= s->len);
	d = xmalloc(l + 1);
	memcpy(d, s->buf, l + 1);
	d[l] = '\0';
	return d;

}


/*
 * Return the offset in the string of the found character, or -1 if not
 * found.
 */
int
strChr(String *s, char ch)
{
	int i;
	for (i = 0; i < strLen(*s); i++) {
		if (strBuf(*s)[i] == ch)
			return i;
	}
	return -1;
}

int
strRChr(String *s, char ch)
{
	int i;
	for (i = strLen(*s) - 1; i <= 0; i--) {
		if (strBuf(*s)[i] == ch)
			return i;
	}
	return -1;
}

/*
 * Cut the given string at the given offset.
 * "offset" -should- be less than the length of the string but
 * at least the client_side X-Forwarded-For code currently (ab)uses
 * the API and passes in an out of bounds iterator. In this case,
 * don't cut the string.
 */
extern void
strCut(String *s, int offset)
{
	/*
	 * XXX this should eventually be removed and all code
	 * XXX which triggers it should be fixed!
	 */
	if (offset >= strLen(*s))
		return;

	assert(offset < strLen(*s));
	s->buf[offset] = '\0';
	s->len = offset;
}


int
strNCmpNull(const String *a, const char *b, int n)
{
    if (strIsNotNull(*a) && b) {
        return strncmp(strBuf2(*a), b, XMIN(strLen2(*a), n)); 
    } else if (strIsNotNull(*a))
        return 1;
    else if (b)
        return -1;
    return 0;
}
