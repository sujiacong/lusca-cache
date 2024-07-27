
/*
 * $Id: htcp.c 14743 2010-08-05 04:50:46Z adrian.chadd $
 *
 * DEBUG: section 31    Hypertext Caching Protocol
 * AUTHOR: Duane Wesssels
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

#include "../libsqurl/url.h"

typedef struct _Countstr Countstr;
typedef struct _htcpHeader htcpHeader;
typedef struct _htcpDataHeader htcpDataHeader;
typedef struct _htcpDataHeaderSquid htcpDataHeaderSquid;
typedef struct _htcpAuthHeader htcpAuthHeader;
typedef struct _htcpStuff htcpStuff;
typedef struct _htcpSpecifier htcpSpecifier;
typedef struct _htcpDetail htcpDetail;

struct _Countstr {
    u_short length;
    char *text;
};

struct _htcpHeader {
    u_short length;
    u_char major;
    u_char minor;
};

struct _htcpDataHeaderSquid {
    u_short length;
#if !WORDS_BIGENDIAN
    unsigned int opcode:4;
    unsigned int response:4;
#else
    unsigned int response:4;
    unsigned int opcode:4;
#endif
#if !WORDS_BIGENDIAN
    unsigned int reserved:6;
    unsigned int F1:1;
    unsigned int RR:1;
#else
    unsigned int RR:1;
    unsigned int F1:1;
    unsigned int reserved:6;
#endif
    u_num32 msg_id;
};

struct _htcpDataHeader {
    u_short length;
#if WORDS_BIGENDIAN
    u_char opcode:4;
    u_char response:4;
#else
    u_char response:4;
    u_char opcode:4;
#endif
#if WORDS_BIGENDIAN
    u_char reserved:6;
    u_char F1:1;
    u_char RR:1;
#else
    u_char RR:1;
    u_char F1:1;
    u_char reserved:6;
#endif
    u_num32 msg_id;
};

    /* RR == 0 --> F1 = RESPONSE DESIRED FLAG */
    /* RR == 1 --> F1 = MESSAGE OVERALL FLAG */
    /* RR == 0 --> REQUEST */
    /* RR == 1 --> RESPONSE */

struct _htcpAuthHeader {
    u_short length;
    time_t sig_time;
    time_t sig_expire;
    Countstr key_name;
    Countstr signature;
};

struct _htcpSpecifier {
    char *method;
    char *uri;
    char *version;
    char *req_hdrs;
    request_t *request;
};

struct _htcpDetail {
    char *resp_hdrs;
    char *entity_hdrs;
    char *cache_hdrs;
};

struct _htcpStuff {
    int op;
    int rr;
    int f1;
    int response;
    int reason;
    u_num32 msg_id;
    htcpSpecifier S;
    htcpDetail D;
};

enum {
    HTCP_NOP,
    HTCP_TST,
    HTCP_MON,
    HTCP_SET,
    HTCP_CLR,
    HTCP_END
};

static const char *const htcpOpcodeStr[] =
{
    "HTCP_NOP",
    "HTCP_TST",
    "HTCP_MON",
    "HTCP_SET",
    "HTCP_CLR",
    "HTCP_END"
};

/*
 * values for htcpDataHeader->response
 */
enum {
    AUTH_REQUIRED,
    AUTH_FAILURE,
    OPCODE_UNIMPLEMENTED,
    MAJOR_VERSION_UNSUPPORTED,
    MINOR_VERSION_UNSUPPORTED,
    INVALID_OPCODE
};

/*
 * values for htcpDataHeader->RR
 */
enum {
    RR_REQUEST,
    RR_RESPONSE
};

static u_num32 msg_id_counter = 0;
static int htcpInSocket = -1;
static int htcpOutSocket = -1;
#define N_QUERIED_KEYS 8192
static u_num32 queried_id[N_QUERIED_KEYS];
static cache_key queried_keys[N_QUERIED_KEYS][SQUID_MD5_DIGEST_LENGTH];
static struct sockaddr_in queried_addr[N_QUERIED_KEYS];
static MemPool *htcpSpecifierPool = NULL;
static MemPool *htcpDetailPool = NULL;

static ssize_t htcpBuildPacket(char *buf, size_t buflen, htcpStuff * stuff);
static htcpSpecifier *htcpUnpackSpecifier(char *buf, int sz);
static htcpDetail *htcpUnpackDetail(char *buf, int sz);
static ssize_t htcpBuildAuth(char *buf, size_t buflen);
static ssize_t htcpBuildCountstr(char *buf, size_t buflen, const char *s);
static ssize_t htcpBuildData(char *buf, size_t buflen, htcpStuff * stuff);
static ssize_t htcpBuildDetail(char *buf, size_t buflen, htcpStuff * stuff);
static ssize_t htcpBuildOpData(char *buf, size_t buflen, htcpStuff * stuff);
static ssize_t htcpBuildSpecifier(char *buf, size_t buflen, htcpStuff * stuff);
static ssize_t htcpBuildTstOpData(char *buf, size_t buflen, htcpStuff * stuff);
static void htcpFreeSpecifier(htcpSpecifier * s);
static void htcpFreeDetail(htcpDetail * s);
static void htcpHandle(char *buf, int sz, struct sockaddr_in *from);
static void htcpHandleMon(htcpDataHeader *, char *buf, int sz, struct sockaddr_in *from);
static void htcpHandleNop(htcpDataHeader *, char *buf, int sz, struct sockaddr_in *from);
static void htcpHandleSet(htcpDataHeader *, char *buf, int sz, struct sockaddr_in *from);
static void htcpHandleTst(htcpDataHeader *, char *buf, int sz, struct sockaddr_in *from);
static void htcpRecv(int fd, void *data);
static void htcpSend(const char *buf, int len, struct sockaddr_in *to);
static void htcpTstReply(htcpDataHeader *, StoreEntry *, htcpSpecifier *, struct sockaddr_in *);
static void htcpHandleTstRequest(htcpDataHeader *, char *buf, int sz, struct sockaddr_in *from);
static void htcpHandleTstResponse(htcpDataHeader *, char *, int, struct sockaddr_in *);
static StoreEntry *htcpCheckHit(const htcpSpecifier *);
static void htcpForwardClr(char *buf, int sz);

static int old_squid_format = 0;

static void
htcpHexdump(const char *tag, const char *s, int sz)
{
#if USE_HEXDUMP
    int i;
    int k;
    char hex[80];
    debugs(31, 3, "htcpHexdump %s", tag);
    memset(hex, '\0', 80);
    for (i = 0; i < sz; i++) {
	k = i % 16;
	snprintf(&hex[k * 3], 4, " %02x", (int) *(s + i));
	if (k < 15 && i < (sz - 1))
	    continue;
	debugs(31, 3, "\t%s", hex);
	memset(hex, '\0', 80);
    }
#endif
}

/*
 * STUFF FOR SENDING HTCP MESSAGES
 */

static ssize_t
htcpBuildAuth(char *buf, size_t buflen)
{
    htcpAuthHeader auth;
    size_t copy_sz = 0;
    assert(2 == sizeof(u_short));
    auth.length = htons(2);
    copy_sz += 2;
    if (buflen < copy_sz)
	return -1;
    xmemcpy(buf, &auth, copy_sz);
    return copy_sz;
}

static ssize_t
htcpBuildCountstr(char *buf, size_t buflen, const char *s)
{
    u_short length;
    size_t len;
    int off = 0;
    if (buflen - off < 2)
	return -1;
    if (s)
	len = strlen(s);
    else
	len = 0;
    debugs(31, 3, "htcpBuildCountstr: LENGTH = %d", (int) len);
    debugs(31, 3, "htcpBuildCountstr: TEXT = {%s}", s ? s : "<NULL>");
    length = htons((u_short) len);
    xmemcpy(buf + off, &length, 2);
    off += 2;
    if (buflen - off < len)
	return -1;
    if (len)
	xmemcpy(buf + off, s, len);
    off += len;
    return off;
}

static ssize_t
htcpBuildSpecifier(char *buf, size_t buflen, htcpStuff * stuff)
{
    ssize_t off = 0;
    ssize_t s;
    s = htcpBuildCountstr(buf + off, buflen - off, stuff->S.method);
    if (s < 0)
	return s;
    off += s;
    s = htcpBuildCountstr(buf + off, buflen - off, stuff->S.uri);
    if (s < 0)
	return s;
    off += s;
    s = htcpBuildCountstr(buf + off, buflen - off, stuff->S.version);
    if (s < 0)
	return s;
    off += s;
    s = htcpBuildCountstr(buf + off, buflen - off, stuff->S.req_hdrs);
    if (s < 0)
	return s;
    off += s;
    debugs(31, 3, "htcpBuildSpecifier: size %d", (int) off);
    return off;
}

static ssize_t
htcpBuildDetail(char *buf, size_t buflen, htcpStuff * stuff)
{
    ssize_t off = 0;
    ssize_t s;
    s = htcpBuildCountstr(buf + off, buflen - off, stuff->D.resp_hdrs);
    if (s < 0)
	return s;
    off += s;
    s = htcpBuildCountstr(buf + off, buflen - off, stuff->D.entity_hdrs);
    if (s < 0)
	return s;
    off += s;
    s = htcpBuildCountstr(buf + off, buflen - off, stuff->D.cache_hdrs);
    if (s < 0)
	return s;
    off += s;
    return off;
}

static ssize_t
htcpBuildTstOpData(char *buf, size_t buflen, htcpStuff * stuff)
{
    switch (stuff->rr) {
    case RR_REQUEST:
	debugs(31, 3, "htcpBuildTstOpData: RR_REQUEST");
	return htcpBuildSpecifier(buf, buflen, stuff);
    case RR_RESPONSE:
	debugs(31, 3, "htcpBuildTstOpData: RR_RESPONSE");
	debugs(31, 3, "htcpBuildTstOpData: F1 = %d", stuff->f1);
	if (stuff->f1)		/* cache miss */
	    return 0;
	else			/* cache hit */
	    return htcpBuildDetail(buf, buflen, stuff);
    default:
	fatal_dump("htcpBuildTstOpData: bad RR value");
    }
    return 0;
}

static ssize_t
htcpBuildClrOpData(char *buf, size_t buflen, htcpStuff * stuff)
{
    u_short reason;

    switch (stuff->rr) {
    case RR_REQUEST:
	debugs(31, 3, "htcpBuildClrOpData: RR_REQUEST");
	reason = htons((u_short) stuff->reason);
	xmemcpy(buf, &reason, 2);
	return htcpBuildSpecifier(buf + 2, buflen - 2, stuff) + 2;
    case RR_RESPONSE:
	break;
    default:
	fatal_dump("htcpBuildClrOpData: bad RR value");
    }
    return 0;
}

static ssize_t
htcpBuildOpData(char *buf, size_t buflen, htcpStuff * stuff)
{
    ssize_t off = 0;
    debugs(31, 3, "htcpBuildOpData: opcode %s",
	htcpOpcodeStr[stuff->op]);
    switch (stuff->op) {
    case HTCP_TST:
	off = htcpBuildTstOpData(buf + off, buflen, stuff);
	break;
    case HTCP_CLR:
	off = htcpBuildClrOpData(buf + off, buflen, stuff);
	break;
    default:
	assert(0);
	break;
    }
    return off;
}

static ssize_t
htcpBuildData(char *buf, size_t buflen, htcpStuff * stuff)
{
    ssize_t off = 0;
    ssize_t op_data_sz;
    size_t hdr_sz = sizeof(htcpDataHeader);
    htcpDataHeader hdr;
    if (buflen < hdr_sz)
	return -1;
    off += hdr_sz;		/* skip! */
    op_data_sz = htcpBuildOpData(buf + off, buflen - off, stuff);
    if (op_data_sz < 0)
	return op_data_sz;
    off += op_data_sz;
    debugs(31, 3, "htcpBuildData: hdr.length = %d", (int) off);
    hdr.length = (u_short) off;
    hdr.opcode = stuff->op;
    hdr.response = stuff->response;
    hdr.RR = stuff->rr;
    hdr.F1 = stuff->f1;
    hdr.msg_id = stuff->msg_id;
    /* convert multi-byte fields */
    hdr.length = htons(hdr.length);
    hdr.msg_id = htonl(hdr.msg_id);
    if (!old_squid_format) {
	xmemcpy(buf, &hdr, hdr_sz);
    } else {
	htcpDataHeaderSquid hdrSquid;
	memset(&hdrSquid, 0, sizeof(hdrSquid));
	hdrSquid.length = hdr.length;
	hdrSquid.opcode = hdr.opcode;
	hdrSquid.response = hdr.response;
	hdrSquid.F1 = hdr.F1;
	hdrSquid.RR = hdr.RR;
	xmemcpy(buf, &hdrSquid, hdr_sz);
    }
    debugs(31, 3, "htcpBuildData: size %d", (int) off);
    return off;
}

/*
 * Build an HTCP packet into buf, maximum length buflen.
 * Returns the packet length, or zero on failure.
 */
static ssize_t
htcpBuildPacket(char *buf, size_t buflen, htcpStuff * stuff)
{
    ssize_t s;
    ssize_t off = 0;
    size_t hdr_sz = sizeof(htcpHeader);
    htcpHeader hdr;
    /* skip the header -- we don't know the overall length */
    if (buflen < hdr_sz) {
	return 0;
    }
    off += hdr_sz;
    s = htcpBuildData(buf + off, buflen - off, stuff);
    if (s < 0) {
	return 0;
    }
    off += s;
    s = htcpBuildAuth(buf + off, buflen - off);
    if (s < 0) {
	return 0;
    }
    off += s;
    hdr.length = htons((u_short) off);
    hdr.major = 0;
    if (old_squid_format)
	hdr.minor = 0;
    else
	hdr.minor = 1;
    xmemcpy(buf, &hdr, hdr_sz);
    debugs(31, 3, "htcpBuildPacket: size %d", (int) off);
    return off;
}

static void
htcpSend(const char *buf, int len, struct sockaddr_in *to)
{
    int x;
    debugs(31, 3, "htcpSend: %s/%d",
	inet_ntoa(to->sin_addr), (int) ntohs(to->sin_port));
    htcpHexdump("htcpSend", buf, len);
    x = comm_udp_sendto(htcpOutSocket,
	to,
	sizeof(struct sockaddr_in),
	buf,
	len);
    if (x < 0)
	debugs(31, 1, "htcpSend: FD %d sendto: %s", htcpOutSocket, xstrerror());
    else
	statCounter.htcp.pkts_sent++;
}

/*
 * STUFF FOR RECEIVING HTCP MESSAGES
 */

static void
htcpFreeSpecifier(htcpSpecifier * s)
{
    if (s->request)
	requestDestroy(s->request);
    memPoolFree(htcpSpecifierPool, s);
}

static void
htcpFreeDetail(htcpDetail * d)
{
    memPoolFree(htcpDetailPool, d);
}

/*
 * Unpack an HTCP SPECIFIER in place
 * This will overwrite any following AUTH block
 */
static htcpSpecifier *
htcpUnpackSpecifier(char *buf, int sz)
{
    htcpSpecifier *s = memPoolAlloc(htcpSpecifierPool);
    method_t *method;

    /* Find length of METHOD */
    u_short l = ntohs(*(u_short *) buf);
    sz -= 2;
    buf += 2;
    if (l > sz) {
	debugs(31, 1, "htcpUnpackSpecifier: failed to unpack METHOD");
	htcpFreeSpecifier(s);
	return NULL;
    }
    /* Set METHOD */
    s->method = buf;
    buf += l;
    sz -= l;

    /* Find length of URI */
    l = ntohs(*(u_short *) buf);
    sz -= 2;
    if (l > sz) {
	debugs(31, 1, "htcpUnpackSpecifier: failed to unpack URI");
	htcpFreeSpecifier(s);
	return NULL;
    }
    /* Add terminating null to METHOD */
    *buf = '\0';
    /* Set URI */
    buf += 2;
    s->uri = buf;
    buf += l;
    sz -= l;

    /* Find length of VERSION */
    l = ntohs(*(u_short *) buf);
    sz -= 2;
    if (l > sz) {
	debugs(31, 1, "htcpUnpackSpecifier: failed to unpack VERSION");
	htcpFreeSpecifier(s);
	return NULL;
    }
    /* Add terminating null to URI */
    *buf = '\0';
    /* Set VERSION */
    buf += 2;
    s->version = buf;
    buf += l;
    sz -= l;

    /* Find length of REQ-HDRS */
    l = ntohs(*(u_short *) buf);
    sz -= 2;
    if (l > sz) {
	debugs(31, 1, "htcpUnpackSpecifier: failed to unpack REQ-HDRS");
	htcpFreeSpecifier(s);
	return NULL;
    }
    /* Add terminating null to URI */
    *buf = '\0';
    /* Set REQ-HDRS */
    buf += 2;
    s->req_hdrs = buf;
    buf += l;
    sz -= l;

    debugs(31, 3, "htcpUnpackSpecifier: %d bytes left", sz);
    /* 
     * Add terminating null to REQ-HDRS. This is possible because we allocated 
     * an extra byte when we received the packet. This will overwrite any following
     * AUTH block.
     */
    *buf = '\0';
    /*
     * Parse the request
     */
    method = urlMethodGetKnown(s->method, strlen(s->method));
    if (method == NULL) {
	method = urlMethodGetKnownByCode(METHOD_GET);
    }
    s->request = urlParse(method, s->uri);
    urlMethodFree(method);
    return s;
}

/*
 * Unpack an HTCP DETAIL in place
 * This will overwrite any following AUTH block
 */
static htcpDetail *
htcpUnpackDetail(char *buf, int sz)
{
    htcpDetail *d = memPoolAlloc(htcpDetailPool);

    /* Find length of RESP-HDRS */
    u_short l = ntohs(*(u_short *) buf);
    sz -= 2;
    buf += 2;
    if (l > sz) {
	debugs(31, 1, "htcpUnpackDetail: failed to unpack RESP-HDRS");
	htcpFreeDetail(d);
	return NULL;
    }
    /* Set RESP-HDRS */
    d->resp_hdrs = buf;
    buf += l;
    sz -= l;

    /* Find length of ENTITY-HDRS */
    l = ntohs(*(u_short *) buf);
    sz -= 2;
    if (l > sz) {
	debugs(31, 1, "htcpUnpackDetail: failed to unpack ENTITY-HDRS");
	htcpFreeDetail(d);
	return NULL;
    }
    /* Add terminating null to RESP-HDRS */
    *buf = '\0';
    /* Set ENTITY-HDRS */
    buf += 2;
    d->entity_hdrs = buf;
    buf += l;
    sz -= l;

    /* Find length of CACHE-HDRS */
    l = ntohs(*(u_short *) buf);
    sz -= 2;
    if (l > sz) {
	debugs(31, 1, "htcpUnpackDetail: failed to unpack CACHE-HDRS");
	htcpFreeDetail(d);
	return NULL;
    }
    /* Add terminating null to ENTITY-HDRS */
    *buf = '\0';
    /* Set CACHE-HDRS */
    buf += 2;
    d->cache_hdrs = buf;
    buf += l;
    sz -= l;

    debugs(31, 3, "htcpUnpackDetail: %d bytes left", sz);
    /* 
     * Add terminating null to CACHE-HDRS. This is possible because we allocated 
     * an extra byte when we received the packet. This will overwrite any following
     * AUTH block.
     */
    *buf = '\0';
    return d;
}

static int
htcpAccessCheck(acl_access * acl, htcpSpecifier * s, struct sockaddr_in *from)
{
    aclCheck_t checklist;
    memset(&checklist, '\0', sizeof(checklist));
    checklist.src_addr = from->sin_addr;
    SetNoAddr(&checklist.my_addr);
    checklist.request = s->request;
    return aclCheckFast(acl, &checklist);
}

static void
htcpTstReply(htcpDataHeader * dhdr, StoreEntry * e, htcpSpecifier * spec, struct sockaddr_in *from)
{
    htcpStuff stuff;
    static char pkt[8192];
    HttpHeader hdr;
    MemBuf mb;
    Packer p;
    ssize_t pktlen;
    char *host;
    int rtt = 0;
    int hops = 0;
    int samp = 0;
    char cto_buf[128];
    memset(&stuff, '\0', sizeof(stuff));
    stuff.op = HTCP_TST;
    stuff.rr = RR_RESPONSE;
    stuff.f1 = 0;
    stuff.response = e ? 0 : 1;
    debugs(31, 3, "htcpTstReply: response = %d", stuff.response);
    stuff.msg_id = dhdr->msg_id;
    if (spec) {
	memBufDefInit(&mb);
	packerToMemInit(&p, &mb);
	httpHeaderInit(&hdr, hoHtcpReply);
	stuff.S.method = spec->method;
	stuff.S.uri = spec->uri;
	stuff.S.version = spec->version;
	stuff.S.req_hdrs = spec->req_hdrs;
	httpHeaderPutInt(&hdr, HDR_AGE,
	    e->timestamp <= squid_curtime ?
	    squid_curtime - e->timestamp : 0);
	httpHeaderPackInto(&hdr, &p);
	stuff.D.resp_hdrs = xstrdup(mb.buf);
	debugs(31, 3, "htcpTstReply: resp_hdrs = {%s}", stuff.D.resp_hdrs);
	memBufReset(&mb);
	httpHeaderReset(&hdr);
	if (e->expires > -1)
	    httpHeaderPutTime(&hdr, HDR_EXPIRES, e->expires);
	if (e->lastmod > -1)
	    httpHeaderPutTime(&hdr, HDR_LAST_MODIFIED, e->lastmod);
	httpHeaderPackInto(&hdr, &p);
	stuff.D.entity_hdrs = xstrdup(mb.buf);
	debugs(31, 3, "htcpTstReply: entity_hdrs = {%s}", stuff.D.entity_hdrs);
	memBufReset(&mb);
	httpHeaderReset(&hdr);
	if ((host = urlHostname(spec->uri))) {
	    netdbHostData(host, &samp, &rtt, &hops);
	    if (rtt || hops) {
		snprintf(cto_buf, 128, "%s %d %f %d",
		    host, samp, 0.001 * rtt, hops);
		httpHeaderPutExt(&hdr, "Cache-to-Origin", cto_buf, -1);
	    }
	}
	httpHeaderPackInto(&hdr, &p);
	stuff.D.cache_hdrs = xstrdup(mb.buf);
	debugs(31, 3, "htcpTstReply: cache_hdrs = {%s}", stuff.D.cache_hdrs);
	memBufClean(&mb);
	httpHeaderClean(&hdr);
	packerClean(&p);
    }
    pktlen = htcpBuildPacket(pkt, sizeof(pkt), &stuff);
    safe_free(stuff.D.resp_hdrs);
    safe_free(stuff.D.entity_hdrs);
    safe_free(stuff.D.cache_hdrs);
    if (!pktlen) {
	debugs(31, 1, "htcpTstReply: htcpBuildPacket() failed");
	return;
    }
    htcpSend(pkt, (int) pktlen, from);
}

static void
htcpClrReply(htcpDataHeader * dhdr, int purgeSucceeded, struct sockaddr_in *from)
{
    htcpStuff stuff;
    static char pkt[8192];
    ssize_t pktlen;

    /* If dhdr->F1 == 0, no response desired */
    if (dhdr->F1 == 0)
	return;

    memset(&stuff, '\0', sizeof(stuff));
    stuff.op = HTCP_CLR;
    stuff.rr = RR_RESPONSE;
    stuff.f1 = 0;
    stuff.response = purgeSucceeded ? 0 : 2;
    debugs(31, 3, "htcpClrReply: response = %d", stuff.response);
    stuff.msg_id = dhdr->msg_id;
    pktlen = htcpBuildPacket(pkt, sizeof(pkt), &stuff);
    if (pktlen == 0) {
	debugs(31, 1, "htcpClrReply: htcpBuildPacket() failed");
	return;
    }
    htcpSend(pkt, (int) pktlen, from);
}

static void
htcpHandleNop(htcpDataHeader * hdr, char *buf, int sz, struct sockaddr_in *from)
{
    debugs(31, 3, "htcpHandleNop: Unimplemented");
}

static StoreEntry *
htcpCheckHit(const htcpSpecifier * s)
{
    request_t *request = s->request;
    StoreEntry *e = NULL, *hit = NULL;
    char *blk_end;
    if (NULL == request) {
	debugs(31, 3, "htcpCheckHit: NO; failed to parse URL");
	return NULL;
    }
    blk_end = s->req_hdrs + strlen(s->req_hdrs);
    if (!httpHeaderParse(&request->header, s->req_hdrs, blk_end)) {
	debugs(31, 3, "htcpCheckHit: NO; failed to parse request headers");
	goto miss;
    }
    e = storeGetPublicByRequest(request);
    if (NULL == e) {
	debugs(31, 3, "htcpCheckHit: NO; public object not found");
	goto miss;
    }
    if (!storeEntryValidToSend(e)) {
	debugs(31, 3, "htcpCheckHit: NO; entry not valid to send");
	goto miss;
    }
    if (refreshCheckHTCP(e, request)) {
	debugs(31, 3, "htcpCheckHit: NO; cached response is stale");
	goto miss;
    }
    debugs(31, 3, "htcpCheckHit: YES!?");
    hit = e;
  miss:
    return hit;
}

static void
htcpClrStoreEntry(StoreEntry * e)
{
    debugs(31, 4, "htcpClrStoreEntry: Clearing store for entry: %s", storeUrl(e));
    storeRelease(e);
}

static int
htcpClrStore(const htcpSpecifier * s)
{
    request_t *request = s->request;
    char *blk_end;
    StoreEntry *e = NULL;
    int released = 0;

    if (request == NULL) {
	debugs(31, 3, "htcpClrStore: failed to parse URL");
	return -1;
    }
    /* Parse request headers */
    blk_end = s->req_hdrs + strlen(s->req_hdrs);
    if (!httpHeaderParse(&request->header, s->req_hdrs, blk_end)) {
	debugs(31, 2, "htcpClrStore: failed to parse request headers");
	return -1;
    }
    /* Lookup matching entries. This matches both GET and HEAD */
    while ((e = storeGetPublicByRequest(request)) != NULL) {
	if (e != NULL) {
	    htcpClrStoreEntry(e);
	    released++;
	}
    }

    if (released) {
	debugs(31, 4, "htcpClrStore: Cleared %d matching entries", released);
	return 1;
    } else {
	debugs(31, 4, "htcpClrStore: No matching entry found");
	return 0;
    }
}

static void
htcpHandleTst(htcpDataHeader * hdr, char *buf, int sz, struct sockaddr_in *from)
{
    debugs(31, 3, "htcpHandleTst: sz = %d", (int) sz);
    if (hdr->RR == RR_REQUEST)
	htcpHandleTstRequest(hdr, buf, sz, from);
    else
	htcpHandleTstResponse(hdr, buf, sz, from);
}

static void
htcpHandleTstResponse(htcpDataHeader * hdr, char *buf, int sz, struct sockaddr_in *from)
{
    htcpReplyData htcpReply;
    cache_key *key = NULL;
    struct sockaddr_in *peer;
    htcpDetail *d = NULL;
    char *t;

    if (queried_id[hdr->msg_id % N_QUERIED_KEYS] != hdr->msg_id) {
	debugs(31, 2, "htcpHandleTstResponse: No matching query id '%d' (expected %d) from '%s'", hdr->msg_id, queried_id[hdr->msg_id % N_QUERIED_KEYS], inet_ntoa(from->sin_addr));
	return;
    }
    key = queried_keys[hdr->msg_id % N_QUERIED_KEYS];
    if (!key) {
	debugs(31, 1, "htcpHandleTstResponse: No query key for response id '%d' from '%s'", hdr->msg_id, inet_ntoa(from->sin_addr));
	return;
    }
    peer = &queried_addr[hdr->msg_id % N_QUERIED_KEYS];
    if (peer->sin_addr.s_addr != from->sin_addr.s_addr || peer->sin_port != from->sin_port) {
	debugs(31, 1, "htcpHandleTstResponse: Unexpected response source %s", inet_ntoa(from->sin_addr));
	return;
    }
    if (hdr->F1 == 1) {
	debugs(31, 2, "htcpHandleTstResponse: error condition, F1/MO == 1");
	return;
    }
    memset(&htcpReply, '\0', sizeof(htcpReply));
    httpHeaderInit(&htcpReply.hdr, hoHtcpReply);
    htcpReply.msg_id = hdr->msg_id;
    debugs(31, 3, "htcpHandleTstResponse: msg_id = %d", (int) htcpReply.msg_id);
    htcpReply.hit = hdr->response ? 0 : 1;
    if (hdr->F1) {
	debugs(31, 3, "htcpHandleTstResponse: MISS");
    } else {
	debugs(31, 3, "htcpHandleTstResponse: HIT");
	d = htcpUnpackDetail(buf, sz);
	if (d == NULL) {
	    debugs(31, 1, "htcpHandleTstResponse: bad DETAIL");
	    return;
	}
	if ((t = d->resp_hdrs))
	    httpHeaderParse(&htcpReply.hdr, t, t + strlen(t));
	if ((t = d->entity_hdrs))
	    httpHeaderParse(&htcpReply.hdr, t, t + strlen(t));
	if ((t = d->cache_hdrs))
	    httpHeaderParse(&htcpReply.hdr, t, t + strlen(t));
    }
    debugs(31, 3, "htcpHandleTstResponse: key (%p) %s", key, storeKeyText(key));
    neighborsHtcpReply(key, &htcpReply, from);
    httpHeaderClean(&htcpReply.hdr);
    if (d)
	htcpFreeDetail(d);
}

static void
htcpHandleTstRequest(htcpDataHeader * dhdr, char *buf, int sz, struct sockaddr_in *from)
{
    /* buf should be a SPECIFIER */
    htcpSpecifier *s;
    StoreEntry *e;
    if (sz == 0) {
	debugs(31, 3, "htcpHandleTstRequest: nothing to do");
	return;
    }
    if (dhdr->F1 == 0)
	return;
    s = htcpUnpackSpecifier(buf, sz);
    if (NULL == s) {
	debugs(31, 2, "htcpHandleTstRequest: htcpUnpackSpecifier failed");
	return;
    }
    if (!s->request) {
	debugs(31, 2, "htcpHandleTstRequest: failed to parse request");
	htcpFreeSpecifier(s);
	return;
    }
    if (!htcpAccessCheck(Config.accessList.htcp, s, from)) {
	debugs(31, 2, "htcpHandleTstRequest: Access denied");
	htcpFreeSpecifier(s);
	return;
    }
    debugs(31, 3, "htcpHandleTstRequest: %s %s %s",
	s->method,
	s->uri,
	s->version);
    debugs(31, 3, "htcpHandleTstRequest: %s", s->req_hdrs);
    if ((e = htcpCheckHit(s)))
	htcpTstReply(dhdr, e, s, from);		/* hit */
    else
	htcpTstReply(dhdr, NULL, NULL, from);	/* cache miss */
    htcpFreeSpecifier(s);
}

static void
htcpHandleMon(htcpDataHeader * hdr, char *buf, int sz, struct sockaddr_in *from)
{
    debugs(31, 3, "htcpHandleMon: Unimplemented");
}

static void
htcpHandleSet(htcpDataHeader * hdr, char *buf, int sz, struct sockaddr_in *from)
{
    debugs(31, 3, "htcpHandleSet: Unimplemented");
}

static void
htcpHandleClr(htcpDataHeader * hdr, char *buf, int sz, struct sockaddr_in *from)
{
    htcpSpecifier *s;
    /* buf[0/1] is reserved and reason */
    int reason = buf[1] << 4;
    debugs(31, 3, "htcpHandleClr: reason=%d", reason);
    buf += 2;
    sz -= 2;

    /* buf should be a SPECIFIER */
    if (sz == 0) {
	debugs(31, 4, "htcpHandleClr: nothing to do");
	return;
    }
    s = htcpUnpackSpecifier(buf, sz);
    if (NULL == s) {
	debugs(31, 3, "htcpHandleClr: htcpUnpackSpecifier failed");
	return;
    }
    if (!s->request) {
	debugs(31, 2, "htcpHandleTstRequest: failed to parse request");
	htcpFreeSpecifier(s);
	return;
    }
    if (!htcpAccessCheck(Config.accessList.htcp_clr, s, from)) {
	debugs(31, 2, "htcpHandleClr: Access denied");
	htcpFreeSpecifier(s);
	return;
    }
    debugs(31, 5, "htcpHandleClr: %s %s %s",
	s->method,
	s->uri,
	s->version);
    debugs(31, 5, "htcpHandleClr: request headers: %s", s->req_hdrs);

    /* Release objects from cache
     * analog to clientPurgeRequest in client_side.c
     */
    switch (htcpClrStore(s)) {
    case 1:
	htcpClrReply(hdr, 1, from);	/* hit */
	break;
    case 0:
	htcpClrReply(hdr, 0, from);	/* miss */
	break;
    default:
	break;
    }

    htcpFreeSpecifier(s);
}

static void
htcpForwardClr(char *buf, int sz)
{
    peer *p;
    int i;

    for (i = 0, p = Config.peers; i < Config.npeers; i++, p = p->next) {
	if (!p->options.htcp) {
	    continue;
	}
	if (!p->options.htcp_forward_clr) {
	    continue;
	}
	htcpSend(buf, sz, &p->in_addr);
    }
}

static void
htcpHandle(char *buf, int sz, struct sockaddr_in *from)
{
    htcpHeader htcpHdr;
    htcpDataHeader hdr;
    char *hbuf;
    int hsz;

    if (sz < sizeof(htcpHeader)) {
	debugs(31, 1, "htcpHandle: msg size less than htcpHeader size");
	return;
    }
    htcpHexdump("htcpHandle", buf, sz);
    xmemcpy(&htcpHdr, buf, sizeof(htcpHeader));
    htcpHdr.length = ntohs(htcpHdr.length);
    if (htcpHdr.minor == 0)
	old_squid_format = 1;
    else
	old_squid_format = 0;
    debugs(31, 3, "htcpHandle: htcpHdr.length = %d", (int) htcpHdr.length);
    debugs(31, 3, "htcpHandle: htcpHdr.major = %d", (int) htcpHdr.major);
    debugs(31, 3, "htcpHandle: htcpHdr.minor = %d", (int) htcpHdr.minor);
    if (sz != htcpHdr.length) {
	debugs(31, 1, "htcpHandle: sz/%d != htcpHdr.length/%d from %s:%d",
	    sz, htcpHdr.length,
	    inet_ntoa(from->sin_addr), (int) ntohs(from->sin_port));
	return;
    }
    if (htcpHdr.major != 0) {
	debugs(31, 1, "htcpHandle: Unknown major version %d from %s:%d",
	    htcpHdr.major,
	    inet_ntoa(from->sin_addr), (int) ntohs(from->sin_port));
	return;
    }
    hbuf = buf + sizeof(htcpHeader);
    hsz = sz - sizeof(htcpHeader);

    if (hsz < sizeof(htcpDataHeader)) {
	debugs(31, 1, "htcpHandle: msg size less than htcpDataHeader size");
	return;
    }
    if (!old_squid_format) {
	xmemcpy(&hdr, hbuf, sizeof(hdr));
    } else {
	htcpDataHeaderSquid hdrSquid;
	xmemcpy(&hdrSquid, hbuf, sizeof(hdrSquid));
	hdr.length = hdrSquid.length;
	hdr.opcode = hdrSquid.opcode;
	hdr.response = hdrSquid.response;
	hdr.F1 = hdrSquid.F1;
	hdr.RR = hdrSquid.RR;
	hdr.reserved = 0;
	hdr.msg_id = hdrSquid.msg_id;
    }
    hdr.length = ntohs(hdr.length);
    hdr.msg_id = ntohl(hdr.msg_id);
    debugs(31, 3, "htcpHandle: hsz = %d", hsz);
    debugs(31, 3, "htcpHandle: length = %d", (int) hdr.length);
    if (hdr.opcode >= HTCP_END) {
	debugs(31, 1, "htcpHandle: client %s, opcode %d out of range",
	    inet_ntoa(from->sin_addr),
	    (int) hdr.opcode);
	return;
    }
    debugs(31, 3, "htcpHandle: opcode = %d %s",
	(int) hdr.opcode, htcpOpcodeStr[hdr.opcode]);
    debugs(31, 3, "htcpHandle: response = %d", (int) hdr.response);
    debugs(31, 3, "htcpHandle: F1 = %d", (int) hdr.F1);
    debugs(31, 3, "htcpHandle: RR = %d", (int) hdr.RR);
    debugs(31, 3, "htcpHandle: msg_id = %d", (int) hdr.msg_id);
    if (hsz < hdr.length) {
	debugs(31, 1, "htcpHandle: hsz < hdr.length");
	return;
    }
    /*
     * set sz = hdr.length so we ignore any AUTH fields following
     * the DATA.
     */
    hsz = (int) hdr.length;
    hbuf += sizeof(htcpDataHeader);
    hsz -= sizeof(htcpDataHeader);
    debugs(31, 3, "htcpHandle: hsz = %d", hsz);
    htcpHexdump("htcpHandle", hbuf, hsz);
    switch (hdr.opcode) {
    case HTCP_NOP:
	htcpHandleNop(&hdr, hbuf, hsz, from);
	break;
    case HTCP_TST:
	htcpHandleTst(&hdr, hbuf, hsz, from);
	break;
    case HTCP_MON:
	htcpHandleMon(&hdr, hbuf, hsz, from);
	break;
    case HTCP_SET:
	htcpHandleSet(&hdr, hbuf, hsz, from);
	break;
    case HTCP_CLR:
	htcpHandleClr(&hdr, hbuf, hsz, from);
	htcpForwardClr(buf, sz);
	break;
    default:
	return;
    }
}

static void
htcpRecv(int fd, void *data)
{
    static char buf[8192];
    int len;
    static struct sockaddr_in from;
    socklen_t flen = sizeof(struct sockaddr_in);
    memset(&from, '\0', flen);
#if NOTYET
    statCounter.syscalls.sock.recvfroms++;
#endif
    /* Receive up to 8191 bytes, leaving room for a null */
    len = recvfrom(fd, buf, sizeof(buf) - 1, 0, (struct sockaddr *) &from, &flen);
    debugs(31, 3, "htcpRecv: FD %d, %d bytes from %s:%d",
	fd, len, inet_ntoa(from.sin_addr), ntohs(from.sin_port));
    if (len)
	statCounter.htcp.pkts_recv++;
    htcpHandle(buf, len, &from);
    commSetSelect(fd, COMM_SELECT_READ, htcpRecv, NULL, 0);
}

static void
htcpIncomingConnectionOpened(int fd, void* data)
{
	if(fd < 0)
	fatal("Cannot open HTCP Socket");
	
    htcpInSocket = fd;
	
    commSetSelect(htcpInSocket, COMM_SELECT_READ, htcpRecv, NULL, 0);
	
    debugs(31, 1, "Accepting HTCP messages on port %d, FD %d.",
	(int) Config.Port.htcp, htcpInSocket);

	fd_note(htcpInSocket, "Incoming HTCP socket");

	if (IsNoAddr(&Config.Addrs.udp_outgoing)) {
		htcpOutSocket = htcpInSocket;
	}
}

/*
 * ======================================================================
 * PUBLIC FUNCTIONS
 * ======================================================================
 */

void
htcpSMPInit(void)
{
    if (Config.Port.htcp <= 0) {
	debugs(31, 1, "HTCP Disabled.");
	return;
    }

	debugs(1, 1, "StrandStartListen htcp in port %d", Config.Port.htcp);

	StrandStartListenRequest(SOCK_DGRAM, IPPROTO_UDP, fdnInIcpSocket, COMM_NONBLOCKING, &Config.Addrs.udp_incoming, Config.Port.htcp, NULL, htcpIncomingConnectionOpened);
	
    if (!IsNoAddr(&Config.Addrs.udp_outgoing)) {
	enter_suid();
	htcpOutSocket = comm_open(SOCK_DGRAM,
	    IPPROTO_UDP,
	    Config.Addrs.udp_outgoing,
	    Config.Port.htcp,
	    COMM_NONBLOCKING,
	    COMM_TOS_DEFAULT,
	    "Outgoing HTCP Socket");
	leave_suid();
	
	if (htcpOutSocket < 0)
	    fatal("Cannot open Outgoing HTCP Socket");
	
	commSetSelect(htcpOutSocket, COMM_SELECT_READ, htcpRecv, NULL, 0);
	
	debugs(31, 1, "Outgoing HTCP messages on port %d, FD %d.", (int) Config.Port.htcp, htcpOutSocket);
	
    } 
	
    if (!htcpSpecifierPool) {
		htcpSpecifierPool = memPoolCreate("htcpSpecifier", sizeof(htcpSpecifier));
		htcpDetailPool = memPoolCreate("htcpDetail", sizeof(htcpDetail));
    }
}

void
htcpInit(void)
{
	if(UsingSmp())
	{
		htcpSMPInit();
	}
	else
	{
	    if (Config.Port.htcp <= 0) {
		debugs(31, 1, "HTCP Disabled.\n");
		return;
	    }
	    enter_suid();
	    htcpInSocket = comm_open(SOCK_DGRAM,
		IPPROTO_UDP,
		Config.Addrs.udp_incoming,
		Config.Port.htcp,
		COMM_NONBLOCKING,
		COMM_TOS_DEFAULT,
		"HTCP Socket");
	    leave_suid();
	    if (htcpInSocket < 0)
		fatal("Cannot open HTCP Socket");
	    commSetSelect(htcpInSocket, COMM_SELECT_READ, htcpRecv, NULL, 0);
	    debugs(31, 1, "Accepting HTCP messages on port %d, FD %d.\n",
		(int) Config.Port.htcp, htcpInSocket);
	    if (! IsNoAddr(&Config.Addrs.udp_outgoing)) {
		enter_suid();
		htcpOutSocket = comm_open(SOCK_DGRAM,
		    IPPROTO_UDP,
		    Config.Addrs.udp_outgoing,
		    Config.Port.htcp,
		    COMM_NONBLOCKING,
		    COMM_TOS_DEFAULT,
		    "Outgoing HTCP Socket");
		leave_suid();
		if (htcpOutSocket < 0)
		    fatal("Cannot open Outgoing HTCP Socket");
		commSetSelect(htcpOutSocket, COMM_SELECT_READ, htcpRecv, NULL, 0);
		debugs(31, 1, "Outgoing HTCP messages on port %d, FD %d.\n",
		    (int) Config.Port.htcp, htcpOutSocket);
		fd_note(htcpInSocket, "Incoming HTCP socket");
	    } else {
		htcpOutSocket = htcpInSocket;
	    }
	    if (!htcpSpecifierPool) {
		htcpSpecifierPool = memPoolCreate("htcpSpecifier", sizeof(htcpSpecifier));
		htcpDetailPool = memPoolCreate("htcpDetail", sizeof(htcpDetail));
	    }
	}
}

void
htcpQuery(StoreEntry * e, request_t * req, peer * p)
{
    cache_key *save_key;
    static char pkt[8192];
    ssize_t pktlen;
    char vbuf[32];
    htcpStuff stuff;
    HttpHeader hdr;
    Packer pa;
    MemBuf mb;
    http_state_flags flags;

    if (htcpInSocket < 0)
	return;

    old_squid_format = p->options.htcp_oldsquid;
    memset(&flags, '\0', sizeof(flags));
    memset(&stuff, '\0', sizeof(stuff));
    snprintf(vbuf, sizeof(vbuf), "%d/%d",
	req->http_ver.major, req->http_ver.minor);
    stuff.op = HTCP_TST;
    stuff.rr = RR_REQUEST;
    stuff.f1 = 1;
    stuff.response = 0;
    stuff.msg_id = ++msg_id_counter;
    stuff.S.method = (char *) urlMethodGetConstStr(req->method);
    stuff.S.uri = (char *) storeUrl(e);
    stuff.S.version = vbuf;
    httpBuildRequestHeader(req, req, e, &hdr, flags);
    memBufDefInit(&mb);
    packerToMemInit(&pa, &mb);
    httpHeaderPackInto(&hdr, &pa);
    httpHeaderClean(&hdr);
    packerClean(&pa);
    stuff.S.req_hdrs = mb.buf;
    pktlen = htcpBuildPacket(pkt, sizeof(pkt), &stuff);
    memBufClean(&mb);
    if (!pktlen) {
	debugs(31, 1, "htcpQuery: htcpBuildPacket() failed");
	return;
    }
    htcpSend(pkt, (int) pktlen, &p->in_addr);
    queried_id[stuff.msg_id % N_QUERIED_KEYS] = stuff.msg_id;
    save_key = queried_keys[stuff.msg_id % N_QUERIED_KEYS];
    storeKeyCopy(save_key, e->hash.key);
    queried_addr[stuff.msg_id % N_QUERIED_KEYS] = p->in_addr;
    debugs(31, 3, "htcpQuery: key (%p) %s", save_key, storeKeyText(save_key));
}

void
htcpClear(StoreEntry * e, const char *uri, request_t * req, method_t * method, peer * p, htcp_clr_reason reason)
{
    static char pkt[8192];
    ssize_t pktlen;
    char vbuf[32];
    htcpStuff stuff;
    HttpHeader hdr;
    Packer pa;
    MemBuf mb;
    http_state_flags flags;

    if (htcpInSocket < 0)
	return;

    old_squid_format = p->options.htcp_oldsquid;
    memset(&flags, '\0', sizeof(flags));
    memset(&stuff, '\0', sizeof(stuff));
    snprintf(vbuf, sizeof(vbuf), "%d/%d",
	req->http_ver.major, req->http_ver.minor);
    stuff.op = HTCP_CLR;
    stuff.rr = RR_REQUEST;
    stuff.f1 = 0;
    stuff.response = 0;
    stuff.msg_id = ++msg_id_counter;
    switch (reason) {
    case HTCP_CLR_INVALIDATION:
	stuff.reason = 1;
	break;
    default:
	stuff.reason = 0;
	break;
    }
    stuff.S.method = (char *) urlMethodGetConstStr(method);
    if (e == NULL || e->mem_obj == NULL) {
	if (uri == NULL) {
	    return;
	}
	stuff.S.uri = xstrdup(uri);
    } else {
	stuff.S.uri = (char *) storeUrl(e);
    }
    stuff.S.version = vbuf;
    if (reason != HTCP_CLR_INVALIDATION) {
	httpBuildRequestHeader(req, req, e, &hdr, flags);
	memBufDefInit(&mb);
	packerToMemInit(&pa, &mb);
	httpHeaderPackInto(&hdr, &pa);
	httpHeaderClean(&hdr);
	packerClean(&pa);
	stuff.S.req_hdrs = mb.buf;
    }
    pktlen = htcpBuildPacket(pkt, sizeof(pkt), &stuff);
    if (reason != HTCP_CLR_INVALIDATION) {
	memBufClean(&mb);
    }
    if (e == NULL) {
	xfree(stuff.S.uri);
    }
    if (!pktlen) {
	debugs(31, 1, "htcpQuery: htcpBuildPacket() failed");
	return;
    }
    htcpSend(pkt, (int) pktlen, &p->in_addr);
}

/*  
 * htcpSocketShutdown only closes the 'in' socket if it is
 * different than the 'out' socket.
 */
void
htcpSocketShutdown(void)
{
    if (htcpInSocket < 0)
	return;
    if (htcpInSocket != htcpOutSocket) {
	debugs(12, 1, "FD %d Closing HTCP socket", htcpInSocket);
	comm_close(htcpInSocket);
    }
    /*      
     * Here we set 'htcpInSocket' to -1 even though the HTCP 'in'
     * and 'out' sockets might be just one FD.  This prevents this
     * function from executing repeatedly.  When we are really ready to
     * exit or restart, main will comm_close the 'out' descriptor.
     */
    htcpInSocket = -1;
    /*      
     * Normally we only write to the outgoing HTCP socket, but
     * we also have a read handler there to catch messages sent
     * to that specific interface.  During shutdown, we must
     * disable reading on the outgoing socket.
     */
    /* XXX Don't we need this handler to read replies while shutting down?
     * I think there should be a separate hander for reading replies..
     */
    assert(htcpOutSocket > -1);
    commSetSelect(htcpOutSocket, COMM_SELECT_READ, NULL, NULL, 0);
}

void
htcpSocketClose(void)
{
    htcpSocketShutdown();
    if (htcpOutSocket > -1) {
	debugs(12, 1, "FD %d Closing HTCP socket", htcpOutSocket);
	comm_close(htcpOutSocket);
	htcpOutSocket = -1;
    }
}
