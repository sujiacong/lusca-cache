
/*
 * $Id: store_key_md5.c 14360 2009-11-04 23:59:48Z adrian.chadd $
 *
 * DEBUG: section 20    Storage Manager MD5 Cache Keys
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

#include "../include/hex.h"

static cache_key null_key[SQUID_MD5_DIGEST_LENGTH];
static MemPool * pool_md5_key = NULL;

const char *
storeKeyText(const unsigned char *key)
{
    static char buf[SQUID_MD5_DIGEST_LENGTH*2+1];

    hex_from_byte_array(buf, (char *) key, SQUID_MD5_DIGEST_LENGTH);
    buf[SQUID_MD5_DIGEST_LENGTH*2] = '\0';

    return buf;
}

const cache_key *
storeKeyScan(const char *buf)
{
    static unsigned char digest[SQUID_MD5_DIGEST_LENGTH];
    int i;
    int j = 0;
    char t[3];
    for (i = 0; i < SQUID_MD5_DIGEST_LENGTH; i++) {
	t[0] = *(buf + (j++));
	t[1] = *(buf + (j++));
	t[2] = '\0';
	*(digest + i) = (unsigned char) strtol(t, NULL, 16);
    }
    return digest;
}

int
storeKeyHashCmp(const void *a, const void *b)
{
    const unsigned char *A = a;
    const unsigned char *B = b;
    int i;
    for (i = 0; i < SQUID_MD5_DIGEST_LENGTH; i++) {
	if (A[i] < B[i])
	    return -1;
	if (A[i] > B[i])
	    return 1;
    }
    return 0;
}

unsigned int
storeKeyHashHash(const void *key, unsigned int n)
{
    /* note, n must be a power of 2! */
    const unsigned char *digest = key;
    unsigned int i = digest[0]
    | digest[1] << 8
    | digest[2] << 16
    | digest[3] << 24;
    return (i & (--n));
}

const cache_key *
storeKeyPrivate(const char *url, method_t * method, int id)
{
    static cache_key digest[SQUID_MD5_DIGEST_LENGTH];
    int zero = 0;
    SQUID_MD5_CTX M;
    assert(id > 0);
    debug(20, 3) ("storeKeyPrivate: %s %s\n", urlMethodGetConstStr(method), url);
    SQUID_MD5Init(&M);
    SQUID_MD5Update(&M, (unsigned char *) &id, sizeof(id));
    if (method == NULL) {
	SQUID_MD5Update(&M, (unsigned char *) &zero, sizeof(int));
    } else {
	SQUID_MD5Update(&M, (unsigned char *) &method->code, sizeof(method->code));
    }
    SQUID_MD5Update(&M, (unsigned char *) url, strlen(url));
    SQUID_MD5Final(digest, &M);
    return digest;
}

const cache_key *
storeKeyPublic(const char *url, const method_t * method)
{
    static cache_key digest[SQUID_MD5_DIGEST_LENGTH];
    unsigned char m;
    SQUID_MD5_CTX M;
    if (method == NULL) {
	m = 0;
    } else {
	m = (unsigned char) method->code;
    }
    SQUID_MD5Init(&M);
    SQUID_MD5Update(&M, &m, sizeof(m));
    SQUID_MD5Update(&M, (unsigned char *) url, strlen(url));
    SQUID_MD5Final(digest, &M);
    return digest;
}

const cache_key *
storeKeyPublicByRequest(request_t * request)
{
    return storeKeyPublicByRequestMethod(request, request->method);
}

const cache_key *
storeKeyPublicByRequestMethod(request_t * request, const method_t * method)
{
    static cache_key digest[SQUID_MD5_DIGEST_LENGTH];
    unsigned char m;
    const char *url;
    SQUID_MD5_CTX M;

    if (method == NULL) {
	m = 0;
    } else {
	m = (unsigned char) method->code;
    }

    if (request->store_url) {
	url = request->store_url;
    } else {
	url = urlCanonical(request);
    }

    SQUID_MD5Init(&M);
    SQUID_MD5Update(&M, &m, sizeof(m));
    SQUID_MD5Update(&M, (unsigned char *) url, strlen(url));
    if (request->vary_headers) {
	SQUID_MD5Update(&M, (unsigned char *) "\0V", 2);
	SQUID_MD5Update(&M, (unsigned char *) request->vary_headers, strlen(request->vary_headers));
	if (strIsNotNull(request->vary_encoding)) {
	    SQUID_MD5Update(&M, (unsigned char *) "\0E", 2);
	    SQUID_MD5Update(&M, (unsigned char *) strBuf2(request->vary_encoding), strLen2(request->vary_encoding));
	}
    }
    if (request->urlgroup) {
	SQUID_MD5Update(&M, (unsigned char *) "\0G", 2);
	SQUID_MD5Update(&M, (unsigned char *) request->urlgroup, strlen(request->urlgroup));
    }
    SQUID_MD5Final(digest, &M);
    return digest;
}

cache_key *
storeKeyDup(const cache_key * key)
{
    cache_key *dup = memPoolAlloc(pool_md5_key);
    xmemcpy(dup, key, SQUID_MD5_DIGEST_LENGTH);
    return dup;
}

cache_key *
storeKeyCopy(cache_key * dst, const cache_key * src)
{
    xmemcpy(dst, src, SQUID_MD5_DIGEST_LENGTH);
    return dst;
}

void
storeKeyFree(const cache_key * key)
{
    memPoolFree(pool_md5_key, (void *) key);
}

int
storeKeyHashBuckets(int nbuckets)
{
    int n = 0x2000;
    while (n < nbuckets)
	n <<= 1;
    return n;
}

int
storeKeyNull(const cache_key * key)
{
    if (memcmp(key, null_key, SQUID_MD5_DIGEST_LENGTH) == 0)
	return 1;
    else
	return 0;
}

void
storeKeyInit(void)
{
    pool_md5_key = memPoolCreate("MD5 digest", SQUID_MD5_DIGEST_LENGTH);
    memset(null_key, '\0', SQUID_MD5_DIGEST_LENGTH);
}
