#include "../include/config.h"

#include <stdio.h>
#if HAVE_STRING_H
#include <string.h>
#endif

#include "proto.h"

const char *ProtocolStr[] =
{
    "NONE",
    "http",
    "ftp",
    "gopher",
    "wais",
    "cache_object",
    "icp",
    "htcp",
    "urn",
    "whois",
    "internal",
    "https",
    "TOTAL"
};

protocol_t
urlParseProtocol(const char *s)
{
    /* test common stuff first */
    if (strcasecmp(s, "http") == 0)
        return PROTO_HTTP;
    if (strcasecmp(s, "ftp") == 0)
        return PROTO_FTP;
    if (strcasecmp(s, "https") == 0)
        return PROTO_HTTPS;
    if (strcasecmp(s, "file") == 0)
        return PROTO_FTP;
    if (strcasecmp(s, "gopher") == 0)
        return PROTO_GOPHER;
    if (strcasecmp(s, "wais") == 0)
        return PROTO_WAIS;
    if (strcasecmp(s, "cache_object") == 0)
        return PROTO_CACHEOBJ;
    if (strcasecmp(s, "urn") == 0)
        return PROTO_URN;
    if (strcasecmp(s, "whois") == 0)
        return PROTO_WHOIS;
    if (strcasecmp(s, "internal") == 0)
        return PROTO_INTERNAL;
    return PROTO_NONE;
}

int
urlDefaultPort(protocol_t p)
{
    switch (p) {
    case PROTO_HTTP:
        return 80;
    case PROTO_HTTPS:
        return 443;
    case PROTO_FTP:
        return 21;
    case PROTO_GOPHER:
        return 70;
    case PROTO_WAIS:
        return 210;
    case PROTO_CACHEOBJ:
    case PROTO_INTERNAL:
        return CACHE_HTTP_PORT;
    case PROTO_WHOIS:
        return 43;
    default:
        return 0;
    }
}

