#include "squid.h"

#include "client_side_ranges.h"

/* XXX */
static const char *const crlf = "\r\n";


/* put terminating boundary for multiparts */
void
clientPackTermBound(String boundary, MemBuf * mb)
{
    memBufPrintf(mb, "\r\n--%.*s--\r\n", strLen2(boundary), strBuf2(boundary));
    debug(33, 6) ("clientPackTermBound: buf offset: %ld\n", (long int) mb->size);
}

/* appends a "part" HTTP header (as in a multi-part/range reply) to the buffer */
void
clientPackRangeHdr(const HttpReply * rep, const HttpHdrRangeSpec * spec, String boundary, MemBuf * mb)
{
    HttpHeader hdr;
    Packer p;
    assert(rep);
    assert(spec);

    /* put boundary */
    debug(33, 5) ("clientPackRangeHdr: appending boundary: %.*s\n", strLen2(boundary), strBuf2(boundary));
    /* rfc2046 requires to _prepend_ boundary with <crlf>! */
    memBufPrintf(mb, "\r\n--%.*s\r\n", strLen2(boundary), strBuf2(boundary));

    /* stuff the header with required entries and pack it */
    httpHeaderInit(&hdr, hoReply);
    if (httpHeaderHas(&rep->header, HDR_CONTENT_TYPE))
	httpHeaderPutStr(&hdr, HDR_CONTENT_TYPE, httpHeaderGetStr(&rep->header, HDR_CONTENT_TYPE));
    httpHeaderAddContRange(&hdr, *spec, rep->content_length);
    packerToMemInit(&p, mb);
    httpHeaderPackInto(&hdr, &p);
    packerClean(&p);
    httpHeaderClean(&hdr);

    /* append <crlf> (we packed a header, not a reply) */
    memBufPrintf(mb, "%s", crlf);
}

/*
 * extracts a "range" from *buf and appends them to mb, updating
 * all offsets and such.
 */
void
clientPackRange(clientHttpRequest * http,
    HttpHdrRangeIter * i,
    const char **buf,
    size_t * size,
    MemBuf * mb)
{
    const size_t copy_sz = i->debt_size <= *size ? i->debt_size : *size;
    squid_off_t body_off = http->out.offset - i->prefix_size;
    assert(*size > 0);
    assert(i->spec);
    /*
     * intersection of "have" and "need" ranges must not be empty
     */
    assert(body_off < i->spec->offset + i->spec->length);
    assert(body_off + *size > i->spec->offset);
    /*
     * put boundary and headers at the beginning of a range in a
     * multi-range
     */
    if (http->request->range->specs.count > 1 && i->debt_size == i->spec->length) {
	assert(http->entry->mem_obj);
	clientPackRangeHdr(
	    http->entry->mem_obj->reply,	/* original reply */
	    i->spec,		/* current range */
	    i->boundary,	/* boundary, the same for all */
	    mb
	    );
    }
    /*
     * append content
     */
    debug(33, 3) ("clientPackRange: appending %ld bytes\n", (long int) copy_sz);
    memBufAppend(mb, *buf, copy_sz);
    /*
     * update offsets
     */
    *size -= copy_sz;
    i->debt_size -= copy_sz;
    body_off += copy_sz;
    *buf += copy_sz;
    http->out.offset = body_off + i->prefix_size;	/* sync */
    /*
     * paranoid check
     */
    assert(i->debt_size >= 0);
}

/* returns true if there is still data available to pack more ranges
 * increments iterator "i"
 * used by clientPackMoreRanges */
int
clientCanPackMoreRanges(const clientHttpRequest * http, HttpHdrRangeIter * i, size_t size)
{
    /* first update "i" if needed */
    if (!i->debt_size) {
	if ((i->spec = httpHdrRangeGetSpec(http->request->range, &i->pos)))
	    i->debt_size = i->spec->length;
    }
    assert(!i->debt_size == !i->spec);	/* paranoid sync condition */
    /* continue condition: need_more_data && have_more_data */
    return i->spec && size > 0;
}

/* extracts "ranges" from buf and appends them to mb, updating all offsets and such */
/* returns true if we need more data */
int
clientPackMoreRanges(clientHttpRequest * http, const char *buf, size_t size, MemBuf * mb)
{
    HttpHdrRangeIter *i = &http->range_iter;
    /* offset in range specs does not count the prefix of an http msg */
    squid_off_t body_off = http->out.offset - i->prefix_size;

    /* check: reply was parsed and range iterator was initialized */
    assert(i->prefix_size > 0);
    /* filter out data according to range specs */
    while (clientCanPackMoreRanges(http, i, size)) {
	squid_off_t start;	/* offset of still missing data */
	assert(i->spec);
	start = i->spec->offset + i->spec->length - i->debt_size;
	debug(33, 3) ("clientPackMoreRanges: in:  offset: %ld size: %ld\n",
	    (long int) body_off, (long int) size);
	debug(33, 3) ("clientPackMoreRanges: out: start: %ld spec[%ld]: [%ld, %ld), len: %ld debt: %ld\n",
	    (long int) start, (long int) i->pos, (long int) i->spec->offset, (long int) (i->spec->offset + i->spec->length), (long int) i->spec->length, (long int) i->debt_size);
	assert(body_off <= start);	/* we did not miss it */
	/* skip up to start */
	if (body_off + size > start) {
	    const size_t skip_size = start - body_off;
	    body_off = start;
	    size -= skip_size;
	    buf += skip_size;
	} else {
	    /* has not reached start yet */
	    body_off += size;
	    size = 0;
	    buf = NULL;
	}
	/* put next chunk if any */
	if (size) {
	    http->out.offset = body_off + i->prefix_size;	/* sync */
	    clientPackRange(http, i, &buf, &size, mb);
	    body_off = http->out.offset - i->prefix_size;	/* sync */
	}
    }
    assert(!i->debt_size == !i->spec);	/* paranoid sync condition */
    debug(33, 3) ("clientPackMoreRanges: buf exhausted: in:  offset: %ld size: %ld need_more: %ld\n",
	(long int) body_off, (long int) size, (long int) i->debt_size);
    if (i->debt_size) {
	debug(33, 3) ("clientPackMoreRanges: need more: spec[%ld]: [%ld, %ld), len: %ld\n",
	    (long int) i->pos, (long int) i->spec->offset, (long int) (i->spec->offset + i->spec->length), (long int) i->spec->length);
	/* skip the data we do not need if possible */
	if (i->debt_size == i->spec->length)	/* at the start of the cur. spec */
	    body_off = i->spec->offset;
	else
	    assert(body_off == i->spec->offset + i->spec->length - i->debt_size);
    } else if (http->request->range->specs.count > 1) {
	/* put terminating boundary for multiparts */
	clientPackTermBound(i->boundary, mb);
    }
    http->out.offset = body_off + i->prefix_size;	/* sync */
    return i->debt_size > 0;
}

/* returns expected content length for multi-range replies
 * note: assumes that httpHdrRangeCanonize has already been called
 * warning: assumes that HTTP headers for individual ranges at the
 *          time of the actuall assembly will be exactly the same as
 *          the headers when clientMRangeCLen() is called */
static squid_off_t
clientMRangeCLen(clientHttpRequest * http)
{
    squid_off_t clen = 0;
    HttpHdrRangePos pos = HttpHdrRangeInitPos;
    const HttpHdrRangeSpec *spec;
    MemBuf mb;

    assert(http->entry->mem_obj);

    memBufDefInit(&mb);
    while ((spec = httpHdrRangeGetSpec(http->request->range, &pos))) {

        /* account for headers for this range */
        memBufReset(&mb);
        clientPackRangeHdr(http->entry->mem_obj->reply,
            spec, http->range_iter.boundary, &mb);
        clen += mb.size;

        /* account for range content */
        clen += spec->length;

        debug(33, 6) ("clientMRangeCLen: (clen += %ld + %" PRINTF_OFF_T ") == %" PRINTF_OFF_T "\n",
            (long int) mb.size, spec->length, clen);
    }
    /* account for the terminating boundary */
    memBufReset(&mb);
    clientPackTermBound(http->range_iter.boundary, &mb);
    clen += mb.size;
            
    memBufClean(&mb);
    return clen;
}

/*
 * returns true if If-Range specs match reply, false otherwise
 */
static int
clientIfRangeMatch(clientHttpRequest * http, HttpReply * rep)
{   
    const TimeOrTag spec = httpHeaderGetTimeOrTag(&http->request->header, HDR_IF_RANGE);
    /* check for parsing falure */
    if (!spec.valid)
        return 0;
    /* got an ETag? */
    if (spec.tag) {
        const char *rep_tag = httpHeaderGetStr(&rep->header, HDR_ETAG);
        debug(33, 3) ("clientIfRangeMatch: ETags: %s and %s\n",
            spec.tag, rep_tag ? rep_tag : "<none>");
        if (!rep_tag)
            return 0;           /* entity has no etag to compare with! */
        if (spec.tag[0] == 'W' || rep_tag[0] == 'W') {
            debug(33, 1) ("clientIfRangeMatch: Weak ETags are not allowed in If-Range: %s ? %s\n",
                spec.tag, rep_tag);
            return 0;           /* must use strong validator for sub-range requests */
        }
        return strcmp(rep_tag, spec.tag) == 0;
    }
    /* got modification time? */
    else if (spec.time >= 0) {
        return http->entry->lastmod == spec.time;
    }
    assert(0);                  /* should not happen */
    return 0;
}

/* adds appropriate Range headers if needed */
void
clientBuildRangeHeader(clientHttpRequest * http, HttpReply * rep)
{
    HttpHeader *hdr = rep ? &rep->header : 0;
    const char *range_err = NULL;
    request_t *request = http->request;
    assert(request->range);
    /* check if we still want to do ranges */
    if (!rep)
        range_err = "no [parse-able] reply";
    else if (rep->sline.status != HTTP_OK)
        range_err = "wrong status code";
    else if (httpHeaderHas(hdr, HDR_CONTENT_RANGE))
        range_err = "origin server does ranges";
    else if (rep->content_length < 0)
        range_err = "unknown length";
    else if (rep->content_length != http->entry->mem_obj->reply->content_length)
        range_err = "INCONSISTENT length";      /* a bug? */
    else if (httpHeaderHas(&http->request->header, HDR_IF_RANGE) && !clientIfRangeMatch(http, rep))
        range_err = "If-Range match failed";
    else if (!httpHdrRangeCanonize(http->request->range, rep->content_length))
        range_err = "canonization failed";
    else if (httpHdrRangeIsComplex(http->request->range))
        range_err = "too complex range header";
    else if (!request->flags.cachable)  /* from we_do_ranges in http.c */
        range_err = "non-cachable request";
    else if (!http->flags.hit && httpHdrRangeOffsetLimit(http->request->range))
        range_err = "range outside range_offset_limit";
    /* get rid of our range specs on error */
    if (range_err) {
        debug(33, 3) ("clientBuildRangeHeader: will not do ranges: %s.\n", range_err);
        httpHdrRangeDestroy(http->request->range);
        http->request->range = NULL;   
    } else {
        const int spec_count = http->request->range->specs.count;
        squid_off_t actual_clen = -1;
            
        debug(33, 3) ("clientBuildRangeHeader: range spec count: %d virgin clen: %" PRINTF_OFF_T "\n",
            spec_count, rep->content_length); 
        assert(spec_count > 0);
        /* append appropriate header(s) */
        if (spec_count == 1) {
            HttpHdrRangePos pos = HttpHdrRangeInitPos;
            const HttpHdrRangeSpec *spec = httpHdrRangeGetSpec(http->request->range, &pos);
            assert(spec);
            /* append Content-Range */
            httpHeaderAddContRange(hdr, *spec, rep->content_length);
            /* set new Content-Length to the actual number of bytes
             * transmitted in the message-body */
            actual_clen = spec->length;
        } else {
            /* multipart! */
            /* generate boundary string */
            http->range_iter.boundary = httpHdrRangeBoundaryStr(http);
            /* delete old Content-Type, add ours */
            httpHeaderDelById(hdr, HDR_CONTENT_TYPE);
            httpHeaderPutStrf(hdr, HDR_CONTENT_TYPE,
                "multipart/byteranges; boundary=\"%.*s\"",
                strLen2(http->range_iter.boundary),
                strBuf2(http->range_iter.boundary));
            /* Content-Length is not required in multipart responses
             * but it is always nice to have one */
            actual_clen = clientMRangeCLen(http);
        }

        /* replace Content-Length header */
        assert(actual_clen >= 0);
        httpHeaderDelById(hdr, HDR_CONTENT_LENGTH);
        httpHeaderPutSize(hdr, HDR_CONTENT_LENGTH, actual_clen);
        rep->content_length = actual_clen;
        debug(33, 3) ("clientBuildRangeHeader: actual content length: %" PRINTF_OFF_T "\n", actual_clen);
    }
}

/*
 * Return true if we should force a cache miss on this range request.
 * entry must be non-NULL.
 */
int
clientCheckRangeForceMiss(StoreEntry * entry, HttpHdrRange * range)
{
    /*
     * If the range_offset_limit is NOT in effect, there
     * is no reason to force a miss.
     */
    if (0 == httpHdrRangeOffsetLimit(range))
        return 0;
    /* 
     * Here, we know it's possibly a hit.  If we already have the
     * whole object cached, we won't force a miss.
     */
    if (STORE_OK == entry->store_status)
        return 0;               /* we have the whole object */
    /*
     * Now we have a hit on a PENDING object.  We need to see
     * if the part we want is already cached.  If so, we don't
     * force a miss.
     */
    assert(NULL != entry->mem_obj);
    if (httpHdrRangeFirstOffset(range) <= entry->mem_obj->inmem_hi)
        return 0;
    /*
     * Even though we have a PENDING copy of the object, we
     * don't want to wait to reach the first range offset,
     * so we force a miss for a new range request to the
     * origin.
     */
    return 1;
}

