#include "squid.h"
#include "ufs_utils.h"

/*
 * ufs_swaplog routines
 */
static int
ufs_swaplog_grow(ufs_swaplog_t *el, int newsize)
{
	storeSwapLogData *b;

	if (newsize < el->size)
		return 1;

	b = realloc(el->buf, (newsize + 16) * sizeof(storeSwapLogData));
	if (! b)
		return -1;
	el->buf = b;
	el->size = newsize + 16;

	return 1;
}

void
ufs_swaplog_init(ufs_swaplog_t *el)
{
	memset(el, 0, sizeof(*el));
	(void) ufs_swaplog_grow(el, 128);
}

void
ufs_swaplog_done(ufs_swaplog_t *el)
{
	safe_free(el->buf);
}

int
ufs_swaplog_append(ufs_swaplog_t *el, StoreEntry *e, int op)
{
    storeSwapLogData *s;

    if (ufs_swaplog_grow(el, el->count + 1)) {
	return -1;
    }
    assert(el->count < el->size);

    s = &el->buf[el->count++];

    s->op = (char) op;
    s->swap_filen = e->swap_filen;
    s->timestamp = e->timestamp;
    s->lastref = e->lastref;
    s->expires = e->expires;
    s->lastmod = e->lastmod;
    s->swap_file_sz = e->swap_file_sz;
    s->refcount = e->refcount;
    s->flags = e->flags;
    xmemcpy(s->key, e->hash.key, SQUID_MD5_DIGEST_LENGTH);

    return 1;
}

size_t
ufs_swaplog_getarray_count(ufs_swaplog_t *el)
{
	return el->count;
}

size_t
ufs_swaplog_getarray_buf_totalsize(ufs_swaplog_t *el)
{
	return el->size;
}

storeSwapLogData *
ufs_swaplog_take_buffer(ufs_swaplog_t *el)
{
	storeSwapLogData *b;

	b = el->buf;
	el->buf = NULL;
	el->count = el->size = 0;
	return b;
}
