
/*
 * $Id: store_swapmeta.c 14504 2010-03-28 07:33:28Z adrian.chadd $
 *
 * DEBUG: section 20    Storage Manager Swapfile Metadata
 * AUTHOR: Kostas Anagnostakis
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

/*
 * Build a TLV list for a StoreEntry
 */
tlv *
storeSwapMetaBuild(StoreEntry * e)
{
    tlv *TLV = NULL;		/* we'll return this */
    tlv **T = &TLV;
    const char *url;
    const char *vary;
    const squid_off_t objsize = objectLen(e);
    assert(e->mem_obj != NULL);
    assert(e->swap_status == SWAPOUT_WRITING);
    url = storeUrl(e);
    debugs(20, 3, "storeSwapMetaBuild: %s", url);
    T = tlv_add(STORE_META_KEY, e->hash.key, SQUID_MD5_DIGEST_LENGTH, T);
#if SIZEOF_SQUID_FILE_SZ == SIZEOF_SIZE_T
    T = tlv_add(STORE_META_STD, &e->timestamp, STORE_HDR_METASIZE, T);
#else
    T = tlv_add(STORE_META_STD_LFS, &e->timestamp, STORE_HDR_METASIZE, T);
#endif
    T = tlv_add(STORE_META_URL, url, strlen(url) + 1, T);
    if (objsize > -1) {
	T = tlv_add(STORE_META_OBJSIZE, &objsize, sizeof(objsize), T);
    }

#if HTTP_GZIP
    if (e->compression_type & SQUID_CACHE_GZIP) {
	T = tlv_add(STORE_META_GZIP, "gzip", sizeof("gzip"), T);
    }
    else if (e->compression_type & SQUID_CACHE_DEFLATE) {
	T = tlv_add(STORE_META_GZIP, "deflate", sizeof("deflate"), T);
    }
#endif

    vary = e->mem_obj->vary_headers;
    if (vary)
	T = tlv_add(STORE_META_VARY_HEADERS, vary, strlen(vary) + 1, T);
    if (e->mem_obj->store_url)
	T = tlv_add(STORE_META_STOREURL, e->mem_obj->store_url, strlen(e->mem_obj->store_url) + 1, T);
    return TLV;
}

static inline char *
storeSwapMetaAssemblePart(char *buf, char t, const void *b, int l)
{
	*buf = (char) t;
	buf++;

	xmemcpy(buf, &l, sizeof(int));
	buf += sizeof(int);

	xmemcpy(buf, b, l);
	buf += l;
	return buf;
}

/*
 * Combined function - take a StoreEntry, return a TLV built for you
 */
char *
storeSwapMetaAssemble(StoreEntry *e, int *length)
{
	char *b, *buf;
	int buflen;
	const squid_off_t objsize = objectLen(e);
	const char *vary, *url, *storeurl;
	int v_len = 0, u_len = 0, s_len = 0;

	/* calculate length of entire buffer */
	vary = e->mem_obj->vary_headers;
	if (vary)
		v_len = strlen(vary);
	url = storeUrl(e);
	u_len = strlen(url);
	storeurl = e->mem_obj->store_url;
	if (storeurl)
		s_len = strlen(storeurl);
		
	/* header byte + length */
	buflen = sizeof(int) + sizeof(char);
	/* add in data - '3' is the extra bytes from the strings! */
	buflen += SQUID_MD5_DIGEST_LENGTH + STORE_HDR_METASIZE + u_len + sizeof(objsize) + v_len + s_len + 3;
	/* add in the type + length for the above */
	buflen += (sizeof(char) + sizeof(int)) * 6;
	b = buf = xmalloc(buflen);

	/* First - SWAP_META_OK */
	b = storeSwapMetaAssemblePart(b, STORE_META_OK, &buflen, sizeof(int));

	/* Meta key */
	b = storeSwapMetaAssemblePart(b, STORE_META_KEY, e->hash.key, SQUID_MD5_DIGEST_LENGTH);

	/* timestamp */
#if SIZEOF_SQUID_FILE_SZ == SIZEOF_SIZE_T
	b = storeSwapMetaAssemblePart(b, STORE_META_STD, &e->timestamp, STORE_HDR_METASIZE);
#else
	b = storeSwapMetaAssemblePart(b, STORE_META_STD_LFS, &e->timestamp, STORE_HDR_METASIZE);
#endif

	/* url */
	b = storeSwapMetaAssemblePart(b, STORE_META_URL, url, u_len + 1);

	/* object size */
	if (objsize > -1) {
		b = storeSwapMetaAssemblePart(b, STORE_META_OBJSIZE, &objsize, sizeof(objsize));
	}

	/* vary headers */
	if (vary)
		b = storeSwapMetaAssemblePart(b, STORE_META_VARY_HEADERS, vary, v_len + 1);

	/* store url */
	if (storeurl)
		b = storeSwapMetaAssemblePart(b, STORE_META_STOREURL, storeurl, s_len + 1);

	/* Finish; return what we did */
	*length = b - buf;
	assert(*length < buflen);
	return buf;
}
