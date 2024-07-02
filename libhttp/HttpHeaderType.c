
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

#include "../libcore/varargs.h"
#include "../libcore/tools.h"

#include "HttpHeaderType.h"

/*
 * A table with major attributes for every known field. 
 * We calculate name lengths and reorganize this array on start up. 
 * After reorganization, field id can be used as an index to the table.
 */
const HttpHeaderFieldAttrs HeadersAttrs[] =
{
    {"Accept", HDR_ACCEPT, ftStr},
    {"Accept-Charset", HDR_ACCEPT_CHARSET, ftStr},
    {"Accept-Encoding", HDR_ACCEPT_ENCODING, ftStr},
    {"Accept-Language", HDR_ACCEPT_LANGUAGE, ftStr},
    {"Accept-Ranges", HDR_ACCEPT_RANGES, ftStr},
    {"Age", HDR_AGE, ftInt},
    {"Allow", HDR_ALLOW, ftStr},
    {"Authorization", HDR_AUTHORIZATION, ftStr},	/* for now */
    {"Cache-Control", HDR_CACHE_CONTROL, ftPCc},
    {"Connection", HDR_CONNECTION, ftStr},
    {"Content-Base", HDR_CONTENT_BASE, ftStr},
    {"Content-Disposition", HDR_CONTENT_DISPOSITION, ftStr},
    {"Content-Encoding", HDR_CONTENT_ENCODING, ftStr},
    {"Content-Language", HDR_CONTENT_LANGUAGE, ftStr},
    {"Content-Length", HDR_CONTENT_LENGTH, ftSize},
    {"Content-Location", HDR_CONTENT_LOCATION, ftStr},
    {"Content-MD5", HDR_CONTENT_MD5, ftStr},	/* for now */
    {"Content-Range", HDR_CONTENT_RANGE, ftPContRange},
    {"Content-Type", HDR_CONTENT_TYPE, ftStr},
    {"Cookie", HDR_COOKIE, ftStr},
    {"Date", HDR_DATE, ftDate_1123},
    {"ETag", HDR_ETAG, ftStr},
    {"Expires", HDR_EXPIRES, ftDate_1123},
    {"From", HDR_FROM, ftStr},
    {"Host", HDR_HOST, ftStr},
    {"If-Match", HDR_IF_MATCH, ftStr},	/* for now */
    {"If-Modified-Since", HDR_IF_MODIFIED_SINCE, ftDate_1123},
    {"If-None-Match", HDR_IF_NONE_MATCH, ftStr},	/* for now */
    {"If-Range", HDR_IF_RANGE, ftDate_1123_or_ETag},
    {"Last-Modified", HDR_LAST_MODIFIED, ftDate_1123},
    {"Link", HDR_LINK, ftStr},
    {"Location", HDR_LOCATION, ftStr},
    {"Max-Forwards", HDR_MAX_FORWARDS, ftInt},
    {"Mime-Version", HDR_MIME_VERSION, ftStr},	/* for now */
    {"Pragma", HDR_PRAGMA, ftStr},
    {"Proxy-Authenticate", HDR_PROXY_AUTHENTICATE, ftStr},
    {"Proxy-Authentication-Info", HDR_PROXY_AUTHENTICATION_INFO, ftStr},
    {"Proxy-Authorization", HDR_PROXY_AUTHORIZATION, ftStr},
    {"Proxy-Connection", HDR_PROXY_CONNECTION, ftStr},
    {"Proxy-support", HDR_PROXY_SUPPORT, ftStr},
    {"Public", HDR_PUBLIC, ftStr},
    {"Range", HDR_RANGE, ftPRange},
    {"Referer", HDR_REFERER, ftStr},
    {"Request-Range", HDR_REQUEST_RANGE, ftPRange},	/* usually matches HDR_RANGE */
    {"Retry-After", HDR_RETRY_AFTER, ftStr},	/* for now (ftDate_1123 or ftInt!) */
    {"Server", HDR_SERVER, ftStr},
    {"Set-Cookie", HDR_SET_COOKIE, ftStr},
    {"Transfer-Encoding", HDR_TRANSFER_ENCODING, ftStr},
    {"Te", HDR_TE, ftStr},
    {"Trailer", HDR_TRAILER, ftStr},
    {"Upgrade", HDR_UPGRADE, ftStr},	/* for now */
    {"User-Agent", HDR_USER_AGENT, ftStr},
    {"Vary", HDR_VARY, ftStr},	/* for now */
    {"Via", HDR_VIA, ftStr},	/* for now */
    {"Expect", HDR_EXPECT, ftStr},
    {"Warning", HDR_WARNING, ftStr},	/* for now */
    {"WWW-Authenticate", HDR_WWW_AUTHENTICATE, ftStr},
    {"Authentication-Info", HDR_AUTHENTICATION_INFO, ftStr},
    {"X-Cache", HDR_X_CACHE, ftStr},
    {"X-Cache-Lookup", HDR_X_CACHE_LOOKUP, ftStr},
    {"X-Forwarded-For", HDR_X_FORWARDED_FOR, ftStr},
    {"X-Request-URI", HDR_X_REQUEST_URI, ftStr},
    {"X-Squid-Error", HDR_X_SQUID_ERROR, ftStr},
    {"X-HTTP09-First-Line", HDR_X_HTTP09_FIRST_LINE, ftStr},
    {"Negotiate", HDR_NEGOTIATE, ftStr},
#if X_ACCELERATOR_VARY
    {"X-Accelerator-Vary", HDR_X_ACCELERATOR_VARY, ftStr},
#endif
    {"X-Error-URL", HDR_X_ERROR_URL, ftStr},
    {"X-Error-Status", HDR_X_ERROR_STATUS, ftInt},
    {"Front-End-Https", HDR_FRONT_END_HTTPS, ftStr},
    {"Keep-Alive", HDR_KEEP_ALIVE, ftStr},
    {"Other:", HDR_OTHER, ftStr}	/* ':' will not allow matches */
};

const int HeadersAttrsCount = countof(HeadersAttrs);

