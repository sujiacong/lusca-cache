
/*
 * $Id: url.c 14700 2010-05-26 09:17:45Z adrian.chadd $
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

#include "squid.h"

#include "../libsqurl/proto.h"
#include "../libsqurl/domain.h"
#include "../libsqurl/url.h"

static request_t *urnParse(method_t * method, char *urn);
static const char valid_hostname_chars_u[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"abcdefghijklmnopqrstuvwxyz"
"0123456789-._";
static const char valid_hostname_chars[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"abcdefghijklmnopqrstuvwxyz"
"0123456789-.";

void
urlInitialize(void)
{
    debug(23, 5) ("urlInitialize: Initializing...\n");
#if 0
    assert(sizeof(ProtocolStr) == (PROTO_MAX + 1) * sizeof(char *));
#endif
    memset(&null_request_flags, '\0', sizeof(null_request_flags));
    /*
     * These test that our matchDomainName() function works the
     * way we expect it to.
     */
    assert(0 == matchDomainName("foo.com", "foo.com"));
    assert(0 == matchDomainName(".foo.com", "foo.com"));
    assert(0 == matchDomainName("foo.com", ".foo.com"));
    assert(0 == matchDomainName(".foo.com", ".foo.com"));
    assert(0 == matchDomainName("x.foo.com", ".foo.com"));
    assert(0 != matchDomainName("x.foo.com", "foo.com"));
    assert(0 != matchDomainName("foo.com", "x.foo.com"));
    assert(0 != matchDomainName("bar.com", "foo.com"));
    assert(0 != matchDomainName(".bar.com", "foo.com"));
    assert(0 != matchDomainName(".bar.com", ".foo.com"));
    assert(0 != matchDomainName("bar.com", ".foo.com"));
    assert(0 < matchDomainName("zzz.com", "foo.com"));
    assert(0 > matchDomainName("aaa.com", "foo.com"));
    assert(0 == matchDomainName("FOO.com", "foo.COM"));
    assert(0 < matchDomainName("bfoo.com", "afoo.com"));
    assert(0 > matchDomainName("afoo.com", "bfoo.com"));
    assert(0 < matchDomainName("x-foo.com", ".foo.com"));
    /* more cases? */
}

/*
 * This routine parses a URL. Its assumed that the URL is complete -
 * ie, the end of the string is the end of the URL. Don't pass a partial
 * URL here as this routine doesn't have any way of knowing whether
 * its partial or not (ie, it handles the case of no trailing slash as
 * being "end of host with implied path of /".
 */
request_t *
urlParse(method_t * method, char *url)
{
    LOCAL_ARRAY(char, proto, MAX_URL);
    LOCAL_ARRAY(char, login, MAX_URL);
    LOCAL_ARRAY(char, host, MAX_URL);
    LOCAL_ARRAY(char, urlpath, MAX_URL);
    request_t *request = NULL;
    char *t = NULL;
    char *q = NULL;
    int port;
    protocol_t protocol = PROTO_NONE;
    int l;
    int i;
    const char *src;
    char *dst;
    proto[0] = host[0] = urlpath[0] = login[0] = '\0';

    if ((l = strlen(url)) + Config.appendDomainLen > (MAX_URL - 1)) {
	/* terminate so it doesn't overflow other buffers */
	*(url + (MAX_URL >> 1)) = '\0';
	debug(23, 1) ("urlParse: URL too large (%d bytes)\n", l);
	return NULL;
    }
    if (method->code == METHOD_CONNECT) {
	port = CONNECT_PORT;
	if (sscanf(url, "%[^:]:%d", host, &port) < 1)
	    return NULL;
    } else if (!strncmp(url, "urn:", 4)) {
	return urnParse(method, url);
    } else {
	/* Parse the URL: */
	src = url;
	i = 0;
	/* Find first : - everything before is protocol */
	for (i = 0, dst = proto; i < l && *src != ':'; i++, src++, dst++) {
	    *dst = *src;
	}
	if (i >= l)
	    return NULL;
	*dst = '\0';

	/* Then its :// */
	/* (XXX yah, I'm not checking we've got enough data left before checking the array..) */
	if (*src != ':' || *(src + 1) != '/' || *(src + 2) != '/')
	    return NULL;
	i += 3;
	src += 3;

	/* Then everything until first /; thats host (and port; which we'll look for here later) */
	/* bug 1881: If we don't get a "/" then we imply it was there */
	for (dst = host; i < l && *src != '/' && src != '\0'; i++, src++, dst++) {
	    *dst = *src;
	}
	/* 
	 * We can't check for "i >= l" here because we could be at the end of the line
	 * and have a perfectly valid URL w/ no trailing '/'. In this case we assume we've
	 * been -given- a valid URL and the path is just '/'.
	 */
	if (i > l)
	    return NULL;
	*dst = '\0';

	/* Then everything from / (inclusive) until \r\n or \0 - thats urlpath */
	for (dst = urlpath; i < l && *src != '\r' && *src != '\n' && *src != '\0'; i++, src++, dst++) {
	    *dst = *src;
	}
	/* We -could- be at the end of the buffer here */
	if (i > l)
	    return NULL;
	/* If the URL path is empty we set it to be "/" */
	if (dst == urlpath) {
	    *(dst++) = '/';
	}
	*dst = '\0';

	protocol = urlParseProtocol(proto);
	port = urlDefaultPort(protocol);
	/* Is there any login informaiton? (we should eventually parse it above) */
	if ((t = strrchr(host, '@'))) {
	    strcpy((char *) login, (char *) host);
	    t = strrchr(login, '@');
	    *t = 0;
	    strcpy((char *) host, t + 1);
	}
	/* Is there any host information? (we should eventually parse it above) */
	if ((t = strrchr(host, ':'))) {
	    *t++ = '\0';
	    if (*t != '\0')
		port = atoi(t);
	}
    }
    for (t = host; *t; t++)
	*t = xtolower(*t);
    if (stringHasWhitespace(host)) {
	if (URI_WHITESPACE_STRIP == Config.uri_whitespace) {
	    t = q = host;
	    while (*t) {
		if (!xisspace(*t))
		    *q++ = *t;
		t++;
	    }
	    *q = '\0';
	}
    }
    if (Config.onoff.check_hostnames && strspn(host, Config.onoff.allow_underscore ? valid_hostname_chars_u : valid_hostname_chars) != strlen(host)) {
	debug(23, 1) ("urlParse: Illegal character in hostname '%s'\n", host);
	return NULL;
    }
    if (Config.appendDomain && !strchr(host, '.'))
	strncat(host, Config.appendDomain, SQUIDHOSTNAMELEN - strlen(host) - 1);
    /* remove trailing dots from hostnames */
    while ((l = strlen(host)) > 0 && host[--l] == '.')
	host[l] = '\0';
    /* reject duplicate or leading dots */
    if (strstr(host, "..") || *host == '.') {
	debug(23, 1) ("urlParse: Illegal hostname '%s'\n", host);
	return NULL;
    }
    if (port < 1 || port > 65535) {
	debug(23, 3) ("urlParse: Invalid port '%d'\n", port);
	return NULL;
    }
#ifdef HARDCODE_DENY_PORTS
    /* These ports are filtered in the default squid.conf, but
     * maybe someone wants them hardcoded... */
    if (port == 7 || port == 9 || port == 19) {
	debug(23, 0) ("urlParse: Deny access to port %d\n", port);
	return NULL;
    }
#endif
    if (stringHasWhitespace(urlpath)) {
	debug(23, 2) ("urlParse: URI has whitespace: {%s}\n", url);
	switch (Config.uri_whitespace) {
	case URI_WHITESPACE_DENY:
	    return NULL;
	case URI_WHITESPACE_ALLOW:
	    break;
	case URI_WHITESPACE_ENCODE:
	    t = rfc1738_escape_unescaped(urlpath);
	    xstrncpy(urlpath, t, MAX_URL);
	    break;
	case URI_WHITESPACE_CHOP:
	    *(urlpath + strcspn(urlpath, w_space)) = '\0';
	    break;
	case URI_WHITESPACE_STRIP:
	default:
	    t = q = urlpath;
	    while (*t) {
		if (!xisspace(*t))
		    *q++ = *t;
		t++;
	    }
	    *q = '\0';
	}
    }
    request = requestCreate(method, protocol, urlpath);
    xstrncpy(request->host, host, SQUIDHOSTNAMELEN);
    xstrncpy(request->login, login, MAX_LOGIN_SZ);
    request->port = (u_short) port;
    return request;
}

static request_t *
urnParse(method_t * method, char *urn)
{
    debug(50, 5) ("urnParse: %s\n", urn);
    return requestCreate(method, PROTO_URN, urn + 4);
}

const char *
urlCanonical(request_t * request)
{
    LOCAL_ARRAY(char, urlbuf, MAX_URL);
    if (request->canonical)
	return request->canonical;
    if (request->protocol == PROTO_URN) {
	snprintf(urlbuf, MAX_URL, "urn:%.*s", strLen2(request->urlpath), strBuf2(request->urlpath));
    } else {
	switch (request->method->code) {
	case METHOD_CONNECT:
	    snprintf(urlbuf, MAX_URL, "%s:%d", request->host, request->port);
	    break;
	default:
	    (void) urlMakeHttpCanonical(urlbuf, request->protocol, request->login,
	      request->host, request->port, strBuf2(request->urlpath), strLen2(request->urlpath));
	    break;
	}
    }
    return (request->canonical = xstrdup(urlbuf));
}

/*
 * Convert a relative URL to an absolute URL using the context of a given
 * request.
 *
 * It is assumed that you have already ensured that the URL is relative.
 *
 * If NULL is returned it is an indication that the method in use in the
 * request does not distinguish between relative and absolute and you should
 * use the url unchanged.
 *
 * If non-NULL is returned, it is up to the caller to free the resulting
 * memory using safe_free().
 */
char *
urlMakeAbsolute(request_t * req, const char *relUrl)
{
    char *urlbuf;
    size_t urllen, pathlen;
    const char *path, *last_slash;

    if (req->method->code == METHOD_CONNECT) {
	return (NULL);
    }
    urlbuf = (char *) xmalloc(MAX_URL * sizeof(char));

    if (req->protocol == PROTO_URN) {
	snprintf(urlbuf, MAX_URL, "urn:%.*s", strLen2(req->urlpath), strBuf2(req->urlpath));
	return (urlbuf);
    }
    if (req->port != urlDefaultPort(req->protocol)) {
	urllen = snprintf(urlbuf, MAX_URL, "%s://%s%s%s:%d",
	    ProtocolStr[req->protocol],
	    req->login,
	    *req->login ? "@" : null_string,
	    req->host,
	    req->port
	    );
    } else {
	urllen = snprintf(urlbuf, MAX_URL, "%s://%s%s%s",
	    ProtocolStr[req->protocol],
	    req->login,
	    *req->login ? "@" : null_string,
	    req->host
	    );
    }

    if (relUrl[0] == '/') {
	strncpy(&urlbuf[urllen], relUrl, MAX_URL - urllen - 1);
    } else {
	path = stringDupToC(&req->urlpath);
	last_slash = strrchr(path, '/');
	if (last_slash == NULL) {
	    urlbuf[urllen++] = '/';
	    strncpy(&urlbuf[urllen], relUrl, MAX_URL - urllen - 1);
	} else {
	    last_slash++;
	    pathlen = last_slash - path;
	    if (pathlen > MAX_URL - urllen - 1) {
		pathlen = MAX_URL - urllen - 1;
	    }
	    strncpy(&urlbuf[urllen], path, pathlen);
	    urllen += pathlen;
	    if (urllen + 1 < MAX_URL) {
		strncpy(&urlbuf[urllen], relUrl, MAX_URL - urllen - 1);
	    }
	}
	safe_free(path);
    }

    return (urlbuf);
}

/*
 * Eventually the request_t strings should be String entries which
 * have in-built length. Eventually we should just take a buffer and
 * do our magic inside that to eliminate that copy.
 */
char *
urlCanonicalClean(const request_t * request)
{
    LOCAL_ARRAY(char, buf, MAX_URL);
    char *t;

    if (request->protocol == PROTO_URN) {
	snprintf(buf, MAX_URL, "urn:%.*s", strLen2(request->urlpath), strBuf2(request->urlpath));
    } else {
	switch (request->method->code) {
	case METHOD_CONNECT:
	    snprintf(buf, MAX_URL, "%s:%d", request->host, request->port);
	    break;
	default:
	    (void) urlMakeHttpCanonical2(buf, request->protocol, request->login,
	      request->host, request->port, strBuf2(request->urlpath), strLen2(request->urlpath));

	    /*
	     * strip arguments AFTER a question-mark
	     */
	    if (Config.onoff.strip_query_terms)
		if ((t = strchr(buf, '?')))
		    *(++t) = '\0';
	    break;
	}
    }
    if (stringHasCntl(buf))
	xstrncpy(buf, rfc1738_escape_unescaped(buf), MAX_URL);
    return buf;
}

int
urlCheckRequest(const request_t * r)
{
    int rc = 0;
    /* protocol "independent" methods */
    if (r->method->code == METHOD_CONNECT)
	return 1;
    if (r->method->code == METHOD_TRACE)
	return 1;
    if (r->method->code == METHOD_PURGE)
	return 1;
    /* does method match the protocol? */
    switch (r->protocol) {
    case PROTO_URN:
    case PROTO_HTTP:
    case PROTO_INTERNAL:
    case PROTO_CACHEOBJ:
	rc = 1;
	break;
    case PROTO_FTP:
	if (r->method->code == METHOD_PUT)
	    rc = 1;
    case PROTO_GOPHER:
    case PROTO_WAIS:
    case PROTO_WHOIS:
	if (r->method->code == METHOD_GET)
	    rc = 1;
	else if (r->method->code == METHOD_HEAD)
	    rc = 1;
	break;
    case PROTO_HTTPS:
#ifdef USE_SSL
	rc = 1;
	break;
#else
	/*
	 * Squid can't originate an SSL connection, so it should
	 * never receive an "https:" URL.  It should always be
	 * CONNECT instead.
	 */
	rc = 0;
#endif
    default:
	break;
    }
    return rc;
}
