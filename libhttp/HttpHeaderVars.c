
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

#include "../libstat/StatHist.h"

#include "../libcb/cbdata.h"

#include "HttpVersion.h"
#include "HttpStatusLine.h"
#include "HttpHeaderType.h"
#include "HttpHeaderFieldStat.h"
#include "HttpHeaderFieldInfo.h"
#include "HttpHeaderEntry.h"
#include "HttpHeader.h"
#include "HttpHeaderVars.h"


/*
 * headers with field values defined as #(values) in HTTP/1.1
 * Headers that are currently not recognized, are commented out.
 */
http_hdr_type ListHeadersArr[] =
{
    HDR_ACCEPT,
    HDR_ACCEPT_CHARSET, HDR_ACCEPT_ENCODING, HDR_ACCEPT_LANGUAGE,
    HDR_ACCEPT_RANGES, HDR_ALLOW,
    HDR_CACHE_CONTROL,
    HDR_CONTENT_ENCODING,
    HDR_CONTENT_LANGUAGE,
    HDR_CONNECTION,
    HDR_IF_MATCH, HDR_IF_NONE_MATCH,
    HDR_LINK, HDR_PRAGMA,
    HDR_PROXY_CONNECTION,
    HDR_PROXY_SUPPORT,
    HDR_TE,
    HDR_TRAILER,
    HDR_TRANSFER_ENCODING,
    HDR_UPGRADE,
    HDR_VARY,
    HDR_VIA,
    /* HDR_WARNING, */
    HDR_WWW_AUTHENTICATE,
    HDR_AUTHENTICATION_INFO,
    HDR_PROXY_AUTHENTICATION_INFO,
    HDR_EXPECT,
#if X_ACCELERATOR_VARY
    HDR_X_ACCELERATOR_VARY,
#endif
    HDR_X_FORWARDED_FOR
};
const int ListHeadersArrCount = countof(ListHeadersArr);

/* general-headers */
http_hdr_type GeneralHeadersArr[] =
{
    HDR_CACHE_CONTROL, HDR_CONNECTION, HDR_DATE, HDR_PRAGMA,
    HDR_TRANSFER_ENCODING,
    HDR_TRAILER,
    HDR_UPGRADE,
    HDR_VIA
};
const int GeneralHeadersArrCount = countof(GeneralHeadersArr);

/* entity-headers */
http_hdr_type EntityHeadersArr[] =
{
    HDR_ALLOW, HDR_CONTENT_BASE, HDR_CONTENT_DISPOSITION,
    HDR_CONTENT_ENCODING, HDR_CONTENT_LANGUAGE, HDR_CONTENT_LENGTH,
    HDR_CONTENT_LOCATION, HDR_CONTENT_MD5, HDR_CONTENT_RANGE,
    HDR_CONTENT_TYPE, HDR_ETAG, HDR_EXPIRES, HDR_LAST_MODIFIED, HDR_LINK,
    HDR_OTHER
};
const int EntityHeadersArrCount = countof(EntityHeadersArr);

http_hdr_type ReplyHeadersArr[] =
{
    HDR_ACCEPT, HDR_ACCEPT_CHARSET, HDR_ACCEPT_ENCODING, HDR_ACCEPT_LANGUAGE,
    HDR_ACCEPT_RANGES, HDR_AGE,
    HDR_LOCATION, HDR_MAX_FORWARDS,
    HDR_MIME_VERSION, HDR_PUBLIC, HDR_RETRY_AFTER, HDR_SERVER, HDR_SET_COOKIE,
    HDR_VARY,
    HDR_WARNING, HDR_PROXY_CONNECTION, HDR_X_CACHE,
    HDR_X_CACHE_LOOKUP,
    HDR_X_REQUEST_URI,
#if X_ACCELERATOR_VARY
    HDR_X_ACCELERATOR_VARY,
#endif
    HDR_X_SQUID_ERROR
};
const int ReplyHeadersArrCount = countof(ReplyHeadersArr);

http_hdr_type RequestHeadersArr[] =
{
    HDR_AUTHORIZATION, HDR_FROM, HDR_HOST,
    HDR_IF_MATCH, HDR_IF_MODIFIED_SINCE, HDR_IF_NONE_MATCH,
    HDR_IF_RANGE, HDR_MAX_FORWARDS, HDR_PROXY_CONNECTION,
    HDR_PROXY_AUTHORIZATION, HDR_RANGE, HDR_REFERER, HDR_REQUEST_RANGE,
    HDR_USER_AGENT, HDR_X_FORWARDED_FOR, HDR_TE, HDR_EXPECT
};
const int RequestHeadersArrCount = countof(RequestHeadersArr);
