#include "../include/config.h"

#include <stdio.h>
#if HAVE_STRING_H
#include <string.h>
#endif
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <sys/types.h>

#include "../include/util.h"

#include "../libcore/tools.h"

#include "proto.h"
#include "defines.h"
#include "url.h"

char *
url_convert_hex(char *org_url, int allocate)
{
    static char code[] = "00";
    char *url = NULL;
    char *s = NULL;
    char *t = NULL;
    url = allocate ? (char *) xstrdup(org_url) : org_url;
    if ((int) strlen(url) < 3 || !strchr(url, '%'))
        return url;
    for (s = t = url; *s; s++) {
        if (*s == '%' && *(s + 1) && *(s + 2)) {
            code[0] = *(++s);
            code[1] = *(++s);
            *t++ = (char) strtol(code, NULL, 16);
        } else {
            *t++ = *s;
        }
    }
    do {
        *t++ = *s;
    } while (*s++);
    return url;
}

/*
 * Test if a URL is relative.
 *
 * RFC 2396, Section 5 (Page 17) implies that in a relative URL, a '/' will
 * appear before a ':'.
 */
int
urlIsRelative(const char *url)
{
    const char *p;

    if (url == NULL) {
        return (0);
    }
    if (*url == '\0') {
        return (0);
    }
    for (p = url; *p != '\0' && *p != ':' && *p != '/'; p++);

    if (*p == ':') {
        return (0);
    }
    return (1);
}

/*
 * Quick-n-dirty host extraction from a URL.  Steps:
 *      Look for a colon
 *      Skip any '/' after the colon
 *      Copy the next SQUID_MAXHOSTNAMELEN bytes to host[]
 *      Look for an ending '/' or ':' and terminate
 *      Look for login info preceeded by '@'
 */
char *
urlHostname(const char *url)
{
    LOCAL_ARRAY(char, host, SQUIDHOSTNAMELEN);
    char *t;
    host[0] = '\0';
    if (NULL == (t = strchr(url, ':')))
        return NULL;
    t++;
    while (*t != '\0' && *t == '/')
        t++;
    xstrncpy(host, t, SQUIDHOSTNAMELEN);
    if ((t = strchr(host, '/')))
        *t = '\0';
    if ((t = strchr(host, ':')))
        *t = '\0';
    if ((t = strrchr(host, '@'))) {
        t++;
        xmemmove(host, t, strlen(t) + 1);
    }
    return host;
}

/*
 * Create a canonical HTTP style URL using the given components.
 *
 * "urlbuf" must be a MAX_URL sized buffer. The NUL terminated URL
 * will be written into that.
 */
int
urlMakeHttpCanonical(char *urlbuf, protocol_t protocol, const char *login,
    const char *host, int port, const char *urlpath, int urlpath_len)
{
	LOCAL_ARRAY(char, portbuf, 32);    
	int len;

	portbuf[0] = '\0';

	if (port != urlDefaultPort(protocol))
		snprintf(portbuf, 32, ":%d", port);
	len = snprintf(urlbuf, MAX_URL, "%s://%s%s%s%s%.*s",
	    ProtocolStr[protocol],
	    login,
	    *login ? "@" : "",
	    host,
	    portbuf,
	    urlpath_len, urlpath);

	return len;
}

int
urlMakeHttpCanonical2(char *urlbuf, protocol_t protocol, const char *login,
    const char *host, int port, const char *urlpath, int urlpath_len)
{
	char *buf = urlbuf;
	LOCAL_ARRAY(char, portbuf, 32);
	LOCAL_ARRAY(char, loginbuf, MAX_LOGIN_SZ + 1);
	char *t;
	int i, j;
	const char *s;
	static const char ts[] = "://";

	portbuf[0] = '\0';
	if (port != urlDefaultPort(protocol))
		snprintf(portbuf, 32, ":%d", port);

	loginbuf[0] = '\0';
	if ((int) strlen(login) > 0) {
		strcpy(loginbuf, login);
		if ((t = strchr(loginbuf, ':')))
			*t = '\0';
		strcat(loginbuf, "@");
	}

	/*
	 * This stuff would be better if/when each of these strings is a String with
	 * a known length..
	*/
	s = ProtocolStr[protocol];
	for (i = 0; i < MAX_URL && *s != '\0'; i++, s++) {
		buf[i] = *s;
	}
	s = ts;
	for (; i < MAX_URL && *s != '\0'; i++, s++) {
		buf[i] = *s;
	}
	s = loginbuf;
	for (; i < MAX_URL && *s != '\0'; i++, s++) {
		buf[i] = *s;
	}
	s = host;
	for (; i < MAX_URL && *s != '\0'; i++, s++) {
		buf[i] = *s;
	}
	s = portbuf;
	for (; i < MAX_URL && *s != '\0'; i++, s++) {
		buf[i] = *s;
	}
	for (j = 0; i < MAX_URL && j < urlpath_len; i++, j++) {
		buf[i] = urlpath[j];
	}

	if (i >= (MAX_URL - 1)) {
		buf[MAX_URL - 1] = '\0';
	} else {
		buf[i] = '\0';
	}

	return i;
}
