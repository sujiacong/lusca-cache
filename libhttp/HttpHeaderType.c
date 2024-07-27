
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
const HttpHeaderFieldAttrs HeadersAttrs[] = {
	{"Accept", HDR_ACCEPT, ftStr},

	{"Accept-Charset", HDR_ACCEPT_CHARSET, ftStr},
	{"Accept-Encoding", HDR_ACCEPT_ENCODING, ftStr},
	{"Accept-Language", HDR_ACCEPT_LANGUAGE, ftStr},
	{"Accept-Ranges", HDR_ACCEPT_RANGES, ftStr},
	{"Age", HDR_AGE, ftInt},
	{"Allow", HDR_ALLOW, ftStr},
	{"Alternate-Protocol", HDR_ALTERNATE_PROTOCOL, ftStr},
	{"Authentication-Info", HDR_AUTHENTICATION_INFO, ftStr},
	{"Authorization", HDR_AUTHORIZATION, ftStr},	/* for now */
	{"Cache-Control", HDR_CACHE_CONTROL, ftPCc},
	{"Connection", HDR_CONNECTION, ftStr},
	{"Content-Base", HDR_CONTENT_BASE, ftStr},
	{"Content-Disposition", HDR_CONTENT_DISPOSITION, ftStr},  /* for now */
	{"Content-Encoding", HDR_CONTENT_ENCODING, ftStr},
	{"Content-Language", HDR_CONTENT_LANGUAGE, ftStr},
	{"Content-Length", HDR_CONTENT_LENGTH, ftInt64},
	{"Content-Location", HDR_CONTENT_LOCATION, ftStr},
	{"Content-MD5", HDR_CONTENT_MD5, ftStr},	/* for now */
	{"Content-Range", HDR_CONTENT_RANGE, ftPContRange},
	{"Content-Type", HDR_CONTENT_TYPE, ftStr},
	{"Cookie", HDR_COOKIE, ftStr},
	{"Cookie2", HDR_COOKIE2, ftStr},
	{"Date", HDR_DATE, ftDate_1123},
	{"ETag", HDR_ETAG, ftETag},
	{"Expect", HDR_EXPECT, ftStr},
	{"Expires", HDR_EXPIRES, ftDate_1123},
	{"Forwarded", HDR_FORWARDED, ftStr},
	{"From", HDR_FROM, ftStr},
	{"Host", HDR_HOST, ftStr},
	{"HTTP2-Settings", HDR_HTTP2_SETTINGS, ftStr}, /* for now */
	{"If-Match", HDR_IF_MATCH, ftStr},	/* for now */
	{"If-Modified-Since", HDR_IF_MODIFIED_SINCE, ftDate_1123},
	{"If-None-Match", HDR_IF_NONE_MATCH, ftStr},	/* for now */
	{"If-Range", HDR_IF_RANGE, ftDate_1123_or_ETag},
	{"If-Unmodified-Since", HDR_IF_UNMODIFIED_SINCE, ftDate_1123},
	{"Keep-Alive", HDR_KEEP_ALIVE, ftStr},
	{"Key", HDR_KEY, ftStr},
	{"Last-Modified", HDR_LAST_MODIFIED, ftDate_1123},
	{"Link", HDR_LINK, ftStr},
	{"Location", HDR_LOCATION, ftStr},
	{"Max-Forwards", HDR_MAX_FORWARDS, ftInt64},
	{"Mime-Version", HDR_MIME_VERSION, ftStr},	/* for now */
	{"Negotiate", HDR_NEGOTIATE, ftStr},
	{"Origin", HDR_ORIGIN, ftStr},
	{"Pragma", HDR_PRAGMA, ftStr},
	{"Proxy-Authenticate", HDR_PROXY_AUTHENTICATE, ftStr},
	{"Proxy-Authentication-Info", HDR_PROXY_AUTHENTICATION_INFO, ftStr},
	{"Proxy-Authorization", HDR_PROXY_AUTHORIZATION, ftStr},
	{"Proxy-Connection", HDR_PROXY_CONNECTION, ftStr},
	{"Proxy-support", HDR_PROXY_SUPPORT, ftStr},
	{"Public", HDR_PUBLIC, ftStr},
	{"Range", HDR_RANGE, ftPRange},
	{"Referer", HDR_REFERER, ftStr},
	{"Request-Range", HDR_REQUEST_RANGE, ftPRange}, /* usually matches HDR_RANGE */
	{"Retry-After", HDR_RETRY_AFTER, ftStr},	/* for now (ftDate_1123 or ftInt!) */
	{"Server", HDR_SERVER, ftStr},
	{"Set-Cookie", HDR_SET_COOKIE, ftStr},
	{"Set-Cookie2", HDR_SET_COOKIE2, ftStr},
	{"TE", HDR_TE, ftStr},
	{"Title", HDR_TITLE, ftStr},
	{"Trailer", HDR_TRAILER, ftStr},
	{"Transfer-Encoding", HDR_TRANSFER_ENCODING, ftStr},
	{"Translate", HDR_TRANSLATE, ftStr},	/* for now. may need to crop */
	{"Unless-Modified-Since", HDR_UNLESS_MODIFIED_SINCE, ftStr},  /* for now ignore. may need to crop */
	{"Upgrade", HDR_UPGRADE, ftStr},	/* for now */
	{"User-Agent", HDR_USER_AGENT, ftStr},
	{"Vary", HDR_VARY, ftStr},	/* for now */
	{"Via", HDR_VIA, ftStr},	/* for now */
	{"Warning", HDR_WARNING, ftStr},	/* for now */
	{"WWW-Authenticate", HDR_WWW_AUTHENTICATE, ftStr},
	{"X-Cache", HDR_X_CACHE, ftStr},
	{"X-Cache-Lookup", HDR_X_CACHE_LOOKUP, ftStr},
	{"X-Forwarded-For", HDR_X_FORWARDED_FOR, ftStr},
	{"X-Request-URI", HDR_X_REQUEST_URI, ftStr},
	{"X-Squid-Error", HDR_X_SQUID_ERROR, ftStr},
#if X_ACCELERATOR_VARY
	{"X-Accelerator-Vary", HDR_X_ACCELERATOR_VARY, ftStr},
#endif
#if USE_ADAPTATION
	{"X-Next-Services", HDR_X_NEXT_SERVICES, ftStr},
#endif
	{"Surrogate-Capability", HDR_SURROGATE_CAPABILITY, ftStr},
	{"Surrogate-Control", HDR_SURROGATE_CONTROL, ftPSc},
	{"Front-End-Https", HDR_FRONT_END_HTTPS, ftStr},
	{"FTP-Command", HDR_FTP_COMMAND, ftStr},
	{"FTP-Arguments", HDR_FTP_ARGUMENTS, ftStr},
	{"FTP-Pre", HDR_FTP_PRE, ftStr},
	{"FTP-Status", HDR_FTP_STATUS, ftInt},
	{"FTP-Reason", HDR_FTP_REASON, ftStr},
	{"X-Error-Status", HDR_X_ERROR_STATUS, ftInt},
	{"X-Error-URL", HDR_X_ERROR_URL, ftStr},
	{"X-HTTP09-First-Line", HDR_X_HTTP09_FIRST_LINE, ftStr},
	{"Other:", HDR_OTHER, ftStr}	/* ':' will not allow matches */
};


const int HeadersAttrsCount = countof(HeadersAttrs);

