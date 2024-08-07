
/*
 * $Id: HttpStatusLine.c 14524 2010-04-01 01:17:09Z adrian.chadd $
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

#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "../include/Array.h"
#include "../include/Stack.h"
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

#include "HttpVersion.h"
#include "HttpStatusLine.h"

/* local constants */
const char *HttpStatusLineFormat = "HTTP/%d.%d %3d %s\r\n";

void
httpStatusLineInit(HttpStatusLine * sline)
{
    http_version_t version;
    httpBuildVersion(&version, 0, 0);
    httpStatusLineSet(sline, version, HTTP_STATUS_NONE, NULL);
}

void
httpStatusLineClean(HttpStatusLine * sline)
{
    http_version_t version;
    httpBuildVersion(&version, 0, 0);
    httpStatusLineSet(sline, version, HTTP_INTERNAL_SERVER_ERROR, NULL);
}

/* set values */
void
httpStatusLineSet(HttpStatusLine * sline, http_version_t version, http_status status, const char *reason)
{
    assert(sline);
    sline->version = version;
    sline->status = status;
    /* Note: no xstrdup for 'reason', assumes constant 'reasons' */
    sline->reason = reason;
}

int
httpStatusLineParse(HttpStatusLine * sline, const char *start, const char *end)
{
    int maj, min, status;
    const char *s;

    assert(sline);
    sline->status = HTTP_INVALID_HEADER;	/* Squid header parsing error */
    if (strncasecmp(start, "HTTP/", 5))
	return 0;
    start += 5;
    if (!xisdigit(*start))
	return 0;

    /* Format: HTTP/x.x <space> <status code> <space> <reason-phrase> CRLF */
    s = start;
    maj = 0;
    for (s = start; s < end && xisdigit(*s) && maj < 65536; s++) {
	maj = maj * 10;
	maj = maj + *s - '0';
    }
    if (s >= end || maj >= 65536) {
	debugs(57, 7, "httpStatusLineParse: Invalid HTTP reply status major.");
	return 0;
    }
    /* next should be '.' */
    if (*s != '.') {
	debugs(57, 7, "httpStatusLineParse: Invalid HTTP reply status line.");
	return 0;
    }
    s++;
    /* next should be minor number */
    min = 0;
    for (; s < end && xisdigit(*s) && min < 65536; s++) {
	min = min * 10;
	min = min + *s - '0';
    }
    if (s >= end || min >= 65536) {
	debugs(57, 7, "httpStatusLineParse: Invalid HTTP reply status version minor.");
	return 0;
    }
    /* then a space */
    if (*s != ' ') {
    }
    s++;
    /* next should be status start */
    status = 0;
    for (; s < end && xisdigit(*s); s++) {
	status = status * 10;
	status = status + *s - '0';
    }
    if (s >= end) {
	debugs(57, 7, "httpStatusLineParse: Invalid HTTP reply status code.");
	return 0;
    }
    /* then a space */

    /* for now we ignore the reason-phrase */

    /* then crlf */

    sline->version.major = maj;
    sline->version.minor = min;
    sline->status = status;

    /* we ignore 'reason-phrase' */
    return 1;			/* success */
}

const char *
httpStatusLineReason(const HttpStatusLine * sline)
{
    assert(sline);
    return sline->reason ? sline->reason : httpStatusString(sline->status);
}

const char *
httpStatusString(http_status status)
{
    /* why not to return matching string instead of using "p" ? @?@ */
    const char *p = NULL;
    switch (status) {
    case 0:
	p = "Init";		/* we init .status with code 0 */
	break;
    case HTTP_CONTINUE:
	p = "Continue";
	break;
    case HTTP_SWITCHING_PROTOCOLS:
	p = "Switching Protocols";
	break;
    case HTTP_OK:
	p = "OK";
	break;
    case HTTP_CREATED:
	p = "Created";
	break;
    case HTTP_ACCEPTED:
	p = "Accepted";
	break;
    case HTTP_NON_AUTHORITATIVE_INFORMATION:
	p = "Non-Authoritative Information";
	break;
    case HTTP_NO_CONTENT:
	p = "No Content";
	break;
    case HTTP_RESET_CONTENT:
	p = "Reset Content";
	break;
    case HTTP_PARTIAL_CONTENT:
	p = "Partial Content";
	break;
    case HTTP_MULTIPLE_CHOICES:
	p = "Multiple Choices";
	break;
    case HTTP_MOVED_PERMANENTLY:
	p = "Moved Permanently";
	break;
    case HTTP_MOVED_TEMPORARILY:
	p = "Moved Temporarily";
	break;
    case HTTP_SEE_OTHER:
	p = "See Other";
	break;
    case HTTP_NOT_MODIFIED:
	p = "Not Modified";
	break;
    case HTTP_USE_PROXY:
	p = "Use Proxy";
	break;
    case HTTP_TEMPORARY_REDIRECT:
	p = "Temporary Redirect";
	break;
    case HTTP_BAD_REQUEST:
	p = "Bad Request";
	break;
    case HTTP_UNAUTHORIZED:
	p = "Unauthorized";
	break;
    case HTTP_PAYMENT_REQUIRED:
	p = "Payment Required";
	break;
    case HTTP_FORBIDDEN:
	p = "Forbidden";
	break;
    case HTTP_NOT_FOUND:
	p = "Not Found";
	break;
    case HTTP_METHOD_NOT_ALLOWED:
	p = "Method Not Allowed";
	break;
    case HTTP_NOT_ACCEPTABLE:
	p = "Not Acceptable";
	break;
    case HTTP_PROXY_AUTHENTICATION_REQUIRED:
	p = "Proxy Authentication Required";
	break;
    case HTTP_REQUEST_TIMEOUT:
	p = "Request Time-out";
	break;
    case HTTP_CONFLICT:
	p = "Conflict";
	break;
    case HTTP_GONE:
	p = "Gone";
	break;
    case HTTP_LENGTH_REQUIRED:
	p = "Length Required";
	break;
    case HTTP_PRECONDITION_FAILED:
	p = "Precondition Failed";
	break;
    case HTTP_REQUEST_ENTITY_TOO_LARGE:
	p = "Request Entity Too Large";
	break;
    case HTTP_REQUEST_URI_TOO_LONG:
	p = "Request-URI Too Long";
	break;
    case HTTP_UNSUPPORTED_MEDIA_TYPE:
	p = "Unsupported Media Type";
	break;
    case HTTP_INTERNAL_SERVER_ERROR:
	p = "Internal Server Error";
	break;
    case HTTP_NOT_IMPLEMENTED:
	p = "Not Implemented";
	break;
    case HTTP_BAD_GATEWAY:
	p = "Bad Gateway";
	break;
    case HTTP_SERVICE_UNAVAILABLE:
	p = "Service Unavailable";
	break;
    case HTTP_GATEWAY_TIMEOUT:
	p = "Gateway Time-out";
	break;
    case HTTP_HTTP_VERSION_NOT_SUPPORTED:
	p = "HTTP Version not supported";
	break;
    case HTTP_EXPECTATION_FAILED:
	p = "Expectation failed";
	break;
    default:
	p = "Unknown";
	debugs(57, 3, "Unknown HTTP status code: %d", status);
	break;
    }
    return p;
}
