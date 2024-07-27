
#include "squid.h"
#include "store_vary.h"

typedef struct {
    StoreEntry *oe;
    StoreEntry *e;
    store_client *sc;
    char *url;
    char *key;
    char *vary_headers;
    char *accept_encoding;
    char *etag;
    squid_off_t seen_offset;
    char *buf;
    size_t buf_size;
    size_t buf_offset;
    int done:1;
    struct {
	char *key;
	char *etag;
	char *accept_encoding;
	int this_key:1;
	int key_used:1;
	int ignore:1;
    } current;
} AddVaryState;

CBDATA_TYPE(AddVaryState);
static void
free_AddVaryState(void *data)
{
    AddVaryState *state = data;
    debugs(11, 2, "free_AddVaryState: %p", data);
    if (!EBIT_TEST(state->e->flags, ENTRY_ABORTED)) {
	storeBuffer(state->e);
	if (!state->done && state->key) {
	    storeAppendPrintf(state->e, "Key: %s\n", state->key);
	    if (state->accept_encoding)
		storeAppendPrintf(state->e, "Accept-Encoding: %s\n", state->accept_encoding);
	    if (state->etag)
		storeAppendPrintf(state->e, "ETag: %s\n", state->etag);
	    storeAppendPrintf(state->e, "VaryData: %s\n", state->vary_headers);
	}
	storeTimestampsSet(state->e);
	storeComplete(state->e);
	storeTimestampsSet(state->e);
	storeBufferFlush(state->e);
    }
    storeUnlockObject(state->e);
    state->e = NULL;
    if (state->sc) {
	storeClientUnregister(state->sc, state->oe, state);
	state->sc = NULL;
    }
    if (state->oe) {
	storeUnlockObject(state->oe);
	state->oe = NULL;
    }
    safe_free(state->url);
    safe_free(state->key);
    safe_free(state->vary_headers);
    safe_free(state->accept_encoding);
    safe_free(state->etag);
    safe_free(state->current.key);
    safe_free(state->current.etag);
    safe_free(state->current.accept_encoding);
    if (state->buf) {
	memFreeBuf(state->buf_size, state->buf);
	state->buf = NULL;
    }
}

static int inline
strmatchbeg(const char *search, const char *match, int maxlen)
{
    int mlen = strlen(match);
    if (maxlen < mlen)
	return -1;
    return strncmp(search, match, mlen);
}

static int inline
strmatch(const char *search, const char *match, int maxlen)
{
    int mlen = strlen(match);
    if (maxlen < mlen)
	return -1;
    return strncmp(search, match, maxlen);
}

static void
storeAddVaryFlush(AddVaryState * state)
{
    if (state->current.ignore || state->current.key_used) {
	/* do nothing */
    } else if (state->current.this_key) {
	if (state->current.key)
	    storeAppendPrintf(state->e, "Key: %s\n", state->current.key);
	else
	    storeAppendPrintf(state->e, "Key: %s\n", state->key);
	if (state->accept_encoding)
	    storeAppendPrintf(state->e, "Accept-Encoding: %s\n", state->accept_encoding);
	if (state->etag)
	    storeAppendPrintf(state->e, "ETag: %s\n", state->etag);
	storeAppendPrintf(state->e, "VaryData: %s\n", state->vary_headers);
	state->done = 1;
	state->current.key_used = 1;
    } else if (state->current.key) {
	storeAppendPrintf(state->e, "Key: %s\n", state->current.key);
	safe_free(state->current.key);
	if (state->current.accept_encoding)
	    storeAppendPrintf(state->e, "Accept-Encoding: %s\n", state->current.accept_encoding);
	if (state->current.etag) {
	    storeAppendPrintf(state->e, "ETag: %s\n", state->current.etag);
	    safe_free(state->current.etag);
	}
	state->current.key_used = 1;
    }
}

static int
strcmpnull(const char *a, const char *b)
{
    if (a && b)
	return strcmp(a, b);
    else if (a)
	return 1;
    else if (b)
	return -1;
    return 0;
}

#if 0
static int
strncmpnull(const char *a, const char *b, size_t n)
{
    if (a && b)
	return strncmp(a, b, n);
    else if (a)
	return 1;
    else if (b)
	return -1;
    return 0;
}
#endif

static void
storeAddVaryReadOld(void *data, mem_node_ref nr, ssize_t size)
{
    AddVaryState *state = data;
    size_t l = size + state->buf_offset;
    char *e;
    char *p = state->buf;
    const char *buf = nr.node->data + nr.offset;

    debugs(11, 3, "storeAddVaryReadOld: %p seen_offset=%" PRINTF_OFF_T " buf_offset=%d size=%d", data, state->seen_offset, (int) state->buf_offset, (int) size);
    if (size <= 0) {
	debugs(11, 2, "storeAddVaryReadOld: DONE");
	cbdataFree(state);
	goto finish;
    }
    assert(size <= nr.node->len);
    /* size should never exceed what we asked for; just make sure first */
    assert(size + state->buf_offset <= state->buf_size);
    /* Copy in the data before we do anything else */
    memcpy(state->buf + state->buf_offset, nr.node->data + nr.offset, size);

    if (EBIT_TEST(state->e->flags, ENTRY_ABORTED)) {
	debugs(11, 1, "storeAddVaryReadOld: New index aborted at %d (%d)", (int) state->seen_offset, (int) size);
	cbdataFree(state);
	goto finish;
    }
    storeBuffer(state->e);
    if (state->seen_offset != 0) {
	state->seen_offset = state->seen_offset + size;
    } else {
	int hdr_sz;
	if (!state->oe->mem_obj->reply)
	    goto invalid_marker_obj;
	if (!strLen2(state->oe->mem_obj->reply->content_type))
	    goto invalid_marker_obj;
	if (strCmp(state->oe->mem_obj->reply->content_type, "x-squid-internal/vary") != 0) {
	  invalid_marker_obj:
	    debugs(11, 2, "storeAddVaryReadOld: %p (%s) is not a Vary maker object, ignoring", data, storeUrl(state->oe));
	    cbdataFree(state);
	    goto finish;
	}
	hdr_sz = state->oe->mem_obj->reply->hdr_sz;
	state->seen_offset = hdr_sz;
	if (l >= hdr_sz) {
	    state->seen_offset = l;
	    l -= hdr_sz;
	    p += hdr_sz;
	} else {
	    l = 0;
	    state->seen_offset = hdr_sz;
	}
    }
    while (l && (e = memchr(p, '\n', l)) != NULL) {
	int l2;
	char *p2;
	if (strmatchbeg(p, "Key: ", l) == 0) {
	    /* key field */
	    p2 = p + 5;
	    l2 = e - p2;
	    if (state->current.this_key) {
		storeAddVaryFlush(state);
	    }
	    safe_free(state->current.key);
	    safe_free(state->current.etag);
	    safe_free(state->current.accept_encoding);
	    memset(&state->current, 0, sizeof(state->current));
	    state->current.key = xmalloc(l2 + 1);
	    memcpy(state->current.key, p2, l2);
	    state->current.key[l2] = '\0';
	    if (state->key) {
		if (strcmp(state->current.key, state->key) == 0)
		    state->current.this_key = 1;
	    }
	    debugs(11, 3, "storeAddVaryReadOld: Key: %s%s", state->current.key, state->current.this_key ? " (THIS)" : "");
#if 0				/* This condition is not correct here.. current.key is always null */
	} else if (!state->current.key) {
	    debugs(11, 1, "storeAddVaryReadOld: Unexpected data '%s'", p);
#endif
	} else if (strmatchbeg(p, "ETag: ", l) == 0) {
	    /* etag field */
	    p2 = p + 6;
	    l2 = e - p2;
	    safe_free(state->current.etag);
	    state->current.etag = xmalloc(l2 + 1);
	    memcpy(state->current.etag, p2, l2);
	    state->current.etag[l2] = '\0';
	    if (state->etag && strcmp(state->current.etag, state->etag) == 0) {
		if (state->accept_encoding && strcmpnull(state->accept_encoding, state->current.accept_encoding) != 0) {
		    /* Skip this match. It's not ours */
		} else if (!state->key) {
		    state->current.this_key = 1;
		} else if (!state->current.this_key) {
		    /* XXX This could use a bit of protection from corrupted entries where Key had not been seen before ETag.. */
		    const cache_key *oldkey = storeKeyScan(state->current.key);
		    StoreEntry *old_e = storeGet(oldkey);
		    if (old_e)
			storeRelease(old_e);
		    if (!state->done) {
			safe_free(state->current.key);
			state->current.key = xstrdup(state->key);
			state->current.this_key = 1;
		    } else {
			state->current.ignore = 1;
		    }
		}
	    } else if (state->current.this_key) {
		state->current.ignore = 1;
	    }
	    debugs(11, 2, "storeAddVaryReadOld: ETag: %s%s%s", state->current.etag, state->current.this_key ? " (THIS)" : "", state->current.ignore ? " (IGNORE)" : "");
	} else if (!state->current.ignore && strmatchbeg(p, "VaryData: ", l) == 0) {
	    /* vary field */
	    p2 = p + 10;
	    l2 = e - p2;
	    storeAddVaryFlush(state);
	    if (strmatch(p2, state->vary_headers, l2) != 0) {
		storeAppend(state->e, p, e - p + 1);
		debugs(11, 3, "storeAddVaryReadOld: %s", p);
	    }
	} else if (strmatchbeg(p, "Accept-Encoding: ", l) == 0) {
	    p2 = p + 17;
	    l2 = e - p2;
	    safe_free(state->current.accept_encoding);
	    state->current.accept_encoding = xmalloc(l2 + 1);
	    memcpy(state->current.accept_encoding, p2, l2);
	    state->current.accept_encoding[l2] = '\0';
	}
	e += 1;
	l -= e - p;
	p = e;
	if (l == 0)
	    break;
	assert(p <= (state->buf + state->buf_offset + size));
    }
    state->buf_offset = l;
    if (l && p != state->buf)
	memmove(state->buf, p, l);
    if (state->buf_offset == state->buf_size) {
	/* Oops.. the buffer size is not sufficient. Grow */
	if (state->buf_size < 65536) {
	    debugs(11, 2, "storeAddVaryReadOld: Increasing entry buffer size to %d", (int) state->buf_size * 2);
	    state->buf = memReallocBuf(state->buf, state->buf_size * 2, &state->buf_size);
	} else {
	    /* This does not look good. Bail out. This should match the size <= 0 case above */
	    debugs(11, 1, "storeAddVaryReadOld: Buffer very large and still can't fit the data.. bailing out");
	    cbdataFree(state);
	    goto finish;
	}
    }
    debugs(11, 3, "storeAddVaryReadOld: %p seen_offset=%" PRINTF_OFF_T " buf_offset=%d", data, state->seen_offset, (int) state->buf_offset);
    storeBufferFlush(state->e);
    storeClientRef(state->sc, state->oe,
	state->seen_offset,
	state->seen_offset,
	state->buf_size - state->buf_offset,
	storeAddVaryReadOld,
	state);
  finish:
    stmemNodeUnref(&nr);
    buf = NULL;
}

/*
 * Adds/updates a Vary record.
 * For updates only one of key or etag needs to be specified
 * At leas one of key or etag must be specified, preferably both.
 */
void
storeAddVary(const char *store_url, const char *url, method_t * method, const cache_key * key, const char *etag, const char *vary, const char *vary_headers, const char *accept_encoding)
{
    AddVaryState *state;
    request_flags flags = null_request_flags;
    CBDATA_INIT_TYPE_FREECB(AddVaryState, free_AddVaryState);
    state = cbdataAlloc(AddVaryState);
    state->url = xstrdup(url);
    if (key)
	state->key = xstrdup(storeKeyText(key));
    state->vary_headers = xstrdup(vary_headers);
    if (accept_encoding)
	state->accept_encoding = xstrdup(accept_encoding);
    if (etag)
	state->etag = xstrdup(etag);
    state->oe = storeGetPublic(store_url ? store_url : url, method);
    debugs(11, 2, "storeAddVary: %s (%s) %s %s",
	state->url, state->key, state->vary_headers, state->etag);
    if (state->oe)
	storeLockObject(state->oe);
    flags.cachable = 1;
    state->e = storeCreateEntry(url, flags, method);
	if (store_url)
	storeEntrySetStoreUrl(state->e, store_url);
    httpReplySetHeaders(state->e->mem_obj->reply, HTTP_OK, "Internal marker object", "x-squid-internal/vary", -1, -1, squid_curtime + 100000);
    httpHeaderPutStr(&state->e->mem_obj->reply->header, HDR_VARY, vary);
    storeSetPublicKey(state->e);
    storeBuffer(state->e);
    httpReplySwapOut(state->e->mem_obj->reply, state->e);
    if (state->oe) {
	/* Here we need to tack on the old etag/vary information, and we should
	 * merge, clean up etc
	 *
	 * Suggestion:
	 * swap in the old file, looking for ETag, Key and VaryData. If a match is
	 * found then 
	 * - on ETag, update the key, and expire the old object if different
	 * - on Key, drop the old data if ETag is different, else nothing
	 * - on VaryData, remove the line if a different key. If this makes
	 *   the searched key "empty" then expire it and remove it from the
	 *   map
	 * - VaryData is added last in the Key record it corresponds to (after
	 *   modifications above)
	 */
	/* Swap in the dummy Vary object */
	if (!state->oe->mem_obj) {
	    storeCreateMemObject(state->oe, state->url);
	    urlMethodAssign(&state->oe->mem_obj->method, method);
	}
	state->sc = storeClientRegister(state->oe, state);
	state->buf = memAllocBuf(4096, &state->buf_size);
	debugs(11, 3, "storeAddVary: %p", state);
	storeClientRef(state->sc, state->oe, 0, 0,
	    state->buf_size,
	    storeAddVaryReadOld,
	    state);
	return;
    } else {
	cbdataFree(state);
    }
}

static MemPool *VaryData_pool = NULL;

void
storeLocateVaryDone(VaryData * data)
{
    int i;
    safe_free(data->key);
    data->etag = NULL;		/* points to an entry in etags */
    for (i = 0; i < data->etags.count; i++) {
	safe_free(data->etags.items[i]);
    }
    arrayClean(&data->etags);
    memPoolFree(VaryData_pool, data);
}

typedef struct {
    VaryData *data;
    STLVCB *callback;
    void *callback_data;
    StoreEntry *e;
    store_client *sc;
    char *buf;
    size_t buf_size;
    size_t buf_offset;
    char *vary_data;
    String accept_encoding;
    squid_off_t seen_offset;
    struct {
	int ignore;
	int encoding_ok;
	char *key;
	char *etag;
    } current;
} LocateVaryState;

CBDATA_TYPE(LocateVaryState);

static void
storeLocateVaryCallback(LocateVaryState * state)
{
    if (cbdataValid(state->callback_data)) {
	VaryData *data = state->data;
	if (data->key || data->etags.count) {
	    state->callback(data, state->callback_data);
	    state->data = NULL;	/* now owned by the caller */
	} else {
	    state->callback(NULL, state->callback_data);
	}
    }
    cbdataUnlock(state->callback_data);
    if (state->data) {
	storeLocateVaryDone(state->data);
	state->data = NULL;
    }
    state->current.etag = NULL;	/* shared by data->entries[x] */
    safe_free(state->vary_data);
    stringClean(&state->accept_encoding);
    safe_free(state->current.key);
    if (state->sc) {
	storeClientUnregister(state->sc, state->e, state);
	state->sc = NULL;
    }
    if (state->e) {
	storeUnlockObject(state->e);
	state->e = NULL;
    }
    if (state->buf) {
	memFreeBuf(state->buf_size, state->buf);
	state->buf = NULL;
    }
    cbdataFree(state);
    debugs(11, 2, "storeLocateVaryCallback: DONE");
}

static void
storeLocateVaryRead(void *data, mem_node_ref nr, ssize_t size)
{
    LocateVaryState *state = data;
    char *e;
    char *p = state->buf;
    size_t l = size + state->buf_offset;
    debugs(11, 3, "storeLocateVaryRead: %s %p seen_offset=%" PRINTF_OFF_T " buf_offset=%d size=%d", state->vary_data, data, state->seen_offset, (int) state->buf_offset, (int) size);
    if (size <= 0) {
	storeLocateVaryCallback(state);
	goto finish;
    }
    assert(size <= nr.node->len);
    /* size should never exceed what we asked for; just make sure first */
    assert(size + state->buf_offset <= state->buf_size);
    /* Copy in the data before we do anything else */
    if (size > 0)
	memcpy(state->buf + state->buf_offset, nr.node->data + nr.offset, size);

    state->seen_offset = state->seen_offset + size;
    while ((e = memchr(p, '\n', l)) != NULL) {
	int l2;
	char *p2;
	if (strmatchbeg(p, "Key: ", l) == 0) {
	    /* key field */
	    p2 = p + 5;
	    l2 = e - p2;
	    safe_free(state->current.key);
	    state->current.etag = NULL;		/* saved in data.etags[] */
	    state->current.ignore = 0;
	    state->current.encoding_ok = strIsNotNull(state->accept_encoding);
	    state->current.key = xmalloc(l2 + 1);
	    memcpy(state->current.key, p2, l2);
	    state->current.key[l2] = '\0';
	    debugs(11, 3, "storeLocateVaryRead: Key: %s", state->current.key);
	} else if (state->current.ignore) {
	    /* Skip this entry */
	} else if (!state->current.key) {
	    char *t1 = xstrndup(p, e - p);
	    char *t2 = xstrndup(state->buf, size + state->buf_offset);
	    debugs(11, 1, "storeLocateVaryRead: Unexpected data '%s' in '%s'", t1, t2);
	    safe_free(t2);
	    safe_free(t1);
	} else if (strmatchbeg(p, "ETag: ", l) == 0) {
	    /* etag field */
	    char *etag;
#if HTTP_GZIP
	    if (!Config.http_gzip.enable) {
#endif
	    if (state->current.encoding_ok) {
		p2 = p + 6;
		l2 = e - p2;
		etag = xmalloc(l2 + 1);
		memcpy(etag, p2, l2);
		etag[l2] = '\0';
		state->current.etag = etag;
		arrayAppend(&state->data->etags, etag);
		debugs(11, 3, "storeLocateVaryRead: ETag: %s", etag);
	    } else {
		state->current.ignore = 1;
	    }
#if HTTP_GZIP
	    }
#endif
	} else if (strmatchbeg(p, "VaryData: ", l) == 0) {
	    /* vary field */
	    p2 = p + 10;
	    l2 = e - p2;
	    if (strmatch(p2, state->vary_data, l2) == 0) {
		/* A matching vary header found */
		safe_free(state->data->key);
		state->data->key = xstrdup(state->current.key);
		state->data->etag = state->current.etag;
		debugs(11, 2, "storeLocateVaryRead: MATCH! %s %s", state->current.key, state->current.etag);
	    }
	} else if (strmatchbeg(p, "Accept-Encoding: ", l) == 0) {
	    p2 = p + 17;
	    l2 = e - p2;
	    /*
	     * This used to use strncmpnull(). It returned 0 on both string equality -and-
	     * both strings being null. Hm! What does this mean if state->accept_encoding String is NULL?
	     * Could p2 ever be NULL? No; it'll be pointing to -something-.
	     */
#if 0
	    if (strncmpnull(state->accept_encoding, p2, l2) == 0 && state->accept_encoding[l2] == '\0') {
#endif
	    if (strNCmpNull(&state->accept_encoding, p2, l2) == 0 && strLen2(state->accept_encoding) == l2) {
		state->current.encoding_ok = 1;
	    }
	}
	e += 1;
	l -= e - p;
	p = e;
	if (l == 0)
	    break;
	assert(l > 0);
	assert(p < (state->buf + state->buf_offset + size));
    }
    state->buf_offset = l;
    if (l)
	memmove(state->buf, p, l);
    if (state->buf_offset == state->buf_size) {
	/* Oops.. the buffer size is not sufficient. Grow */
	if (state->buf_size < 65536) {
	    debugs(11, 2, "storeLocateVaryRead: Increasing entry buffer size to %d", (int) state->buf_size * 2);
	    state->buf = memReallocBuf(state->buf, state->buf_size * 2, &state->buf_size);
	} else {
	    /* This does not look good. Bail out. This should match the size <= 0 case above */
	    debugs(11, 1, "storeLocateVaryRead: Buffer very large and still can't fit the data.. bailing out");
	    storeLocateVaryCallback(state);
	    goto finish;
	}
    }
    debugs(11, 3, "storeLocateVaryRead: %p seen_offset=%" PRINTF_OFF_T " buf_offset=%d", data, state->seen_offset, (int) state->buf_offset);
    storeClientRef(state->sc, state->e,
	state->seen_offset,
	state->seen_offset,
	state->buf_size - state->buf_offset,
	storeLocateVaryRead,
	state);
  finish:
    stmemNodeUnref(&nr);
}

void
storeLocateVary(StoreEntry * e, int offset, const char *vary_data, String accept_encoding, STLVCB * callback, void *cbdata)
{
    LocateVaryState *state;
    debugs(11, 2, "storeLocateVary: %s", vary_data);
    CBDATA_INIT_TYPE(LocateVaryState);
    if (!VaryData_pool)
	VaryData_pool = memPoolCreate("VaryData", sizeof(VaryData));
    state = cbdataAlloc(LocateVaryState);
    state->vary_data = xstrdup(vary_data);
    if (strIsNotNull(accept_encoding))
	state->accept_encoding = stringDup(&accept_encoding);
    state->data = memPoolAlloc(VaryData_pool);
    state->e = e;
    storeLockObject(state->e);
    state->callback_data = cbdata;
    cbdataLock(cbdata);
    state->callback = callback;
    state->buf = memAllocBuf(4096, &state->buf_size);
    state->sc = storeClientRegister(state->e, state);
    state->seen_offset = offset;
    if (!strLen2(e->mem_obj->reply->content_type) || strCmp(e->mem_obj->reply->content_type, "x-squid-internal/vary") != 0) {
	/* This is not our Vary marker object. Bail out. */
	debugs(33, 1, "storeLocateVary: Not our vary marker object, %s = '%s', vary_data='%s' ; content-type: '%.*s' ; accept_encoding='%.*s'",
	    storeKeyText(e->hash.key), e->mem_obj->url, vary_data,
	    strLen2(e->mem_obj->reply->content_type) ? strLen2(e->mem_obj->reply->content_type) : 1,
	    strBuf2(e->mem_obj->reply->content_type) ? strBuf2(e->mem_obj->reply->content_type) : "-",
	    strLen2(accept_encoding) ? strLen2(accept_encoding) : 1,
	    strBuf2(accept_encoding) ? strBuf2(accept_encoding) : "-");

	if (strLen2(e->mem_obj->reply->content_type))
		debugs(33, 1, "storeLocateVary: local content type: '%.*s'",
		    strLen2(e->mem_obj->reply->content_type),
		    strBuf2(e->mem_obj->reply->content_type));
	else
		debugs(33, 1, "storeLocateVary: reply->content_type length is 0, why!?");

	storeLocateVaryCallback(state);
	return;
    }
    storeClientRef(state->sc, state->e,
	state->seen_offset,
	state->seen_offset,
	state->buf_size,
	storeLocateVaryRead,
	state);
}
