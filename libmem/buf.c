#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "../include/util.h"

#include "../include/Stack.h"
#include "../libcore/varargs.h"
#include "../libcore/debug.h"
#include "../libcore/tools.h"
#include "../libcore/gb.h"
#include "../libcore/kb.h"
#include "../libcore/dlink.h"

#include "../libmem/MemPool.h"

#include "buf.h"

static int buf_configured = 0;
static MemPool *buf_pool = NULL;

#if	BUF_TRACK_BUFS
dlink_list buf_active_list;
#endif

int buf_active_num = 0;

void
buf_init(void)
{
	if (buf_configured)
		return;

	assert(buf_pool == NULL);
	if (! buf_pool)
		buf_pool = memPoolCreate("buf_t", sizeof(buf_t));
}


int
buf_changesize(buf_t *b, int newsize)
{
	char *p;

	/*
	 * buffer shouldn't ever be smaller than 'len', but we'll try
	 * to handle it..
	 */
	debug (85, 5) ("buf_changesize: %p: size %d -> %d\n", b, b->size, newsize);
        if (newsize <= b->size)
            return 1;
	assert(b->flags.isfinal == 0);
	/* can't reallocate a fixed buffer! */
	if (b->flags.isfinal != 0)
		return 0;

	p = realloc(b->b, newsize);
	if (! p)
		return 0;
	b->b = p;
	b->size = newsize;
	/* truncate buffer length if/where possible */
	if (b->len > b->size) {
		b->len = b->size;
	}
	return 1;
}

/*
 * Grow the buffer to accomodate the given minimum free size.
 * XXX Ideally we'll want to page size align the eventual allocation.
 */
int
buf_grow_to_min_free(buf_t *b, int minfree)
{
	if (buf_remainder(b) >= minfree)
		return 0;
	return buf_changesize(b, b->size + minfree);
}


buf_t *
buf_create_int(const char *file, int line)
{
	buf_t *b;

	b = memPoolAlloc(buf_pool);
	if (! b)
		return NULL;
#if BUF_TRACK_BUFS
	dlinkAddTail(b, &b->node, &buf_active_list);
#endif
	buf_active_num++;
	debug (85, 5) ("buf_create: %p\n", b);
	buf_ref(b);
	b->create.file = file;
	b->create.line = line;
	return b;
}

buf_t *
buf_create_size_int(int size, const char *file, int line)
{
	buf_t *b;
	b = buf_create();
	if (!b)
		return NULL;
	if (! buf_changesize(b, size)) {
		buf_deref(b);
		return NULL;
	}
	b->create.file = file;
	b->create.line = line;
	return b;
}

buf_t *
buf_create_const_int(const void *data, size_t len, const char *file, int line)
{
	buf_t *b;

	b = memPoolAlloc(buf_pool);
	if (! b)
		return NULL;
#if BUF_TRACK_BUFS
	dlinkAddTail(b, &b->node, &buf_active_list);
#endif
	buf_active_num++;
	debug(85, 5) ("buf_create: %p\n", b);
	b->b = (char *)data;
	b->len = b->size = b->sofs = len;
	b->flags.isconst = 1;
	b->flags.isfinal = 1;
	b->create.file = file;
	b->create.line = line;
	buf_ref(b);
	return b;
}

void
buf_free(buf_t *b)
{
	assert(b->flags.isfreed == 0);
	debug (85, 5) ("buf_free: %p\n", b);
	if (b->flags.isfreed == 0) {
		b->flags.isfreed = 1;
		(void) buf_deref(b);
	}
	/* b could be invalid at this point */
}

buf_t *
buf_ref(buf_t *b)
{
	debug(85, 5) ("buf_ref: %p\n", b);
	b->nref++;
	return b;	/* for now, this might change .. */
}

/* XXX this should properly NULL out the pointer thats being held.. */
buf_t *
buf_deref(buf_t *b)
{
	assert(b->nref >= 1);
	b->nref--;
	debug(85, 5) ("buf_deref: %p\n", b);
	/* the free actually goes on 'ere */
	if (b->nref == 0) {
		debug (85, 5) ("buf_deref: %p: FREEing\n", b);
		if (!b->flags.isconst) {
		    free(b->b); b->b = NULL;
		}
#if BUF_TRACK_BUFS
                dlinkDelete(&b->node, &buf_active_list);
#endif
		buf_active_num--;
		memPoolFree(buf_pool, b);
		return NULL;
	}
	/* b could be invalid here */
	return NULL;
}

int
buf_fill(buf_t *b, int fd, int dogrow)
{
	int ret;
	/* for now, always make sure there's 16k available space */
	debug(85, 5) ("buf_fill: fd %d\n", fd);
	if (dogrow && b->size - b->len < 8192) {
		if (! buf_changesize(b, b->size + 8192)) {
			/* tsk! */
			return -1;
		}
	}
	/* XXX we should never be given a full buffer to fill! */
	assert(!buf_isfull(b));
	ret = read(fd, b->b + b->len, b->size - b->len);
	debug(85, 5) ("buf_fill: fd %d read %d bytes\n", fd, ret);
	if (ret > 0) {
		b->len += ret;
	}
	return ret;
}

int
buf_append(buf_t *b, const void *src, size_t len, buf_flags_t flags)
{
        /* only grow the buffer if we need to */
        int bl = 0;
	if (flags & BF_APPEND_NUL)
		bl = 1;
 
	if (!buf_changesize(b, b->len + len + bl)) {
		/* tsk! */
		return -1;
	}
        assert(b->len + len <= b->size);
	memcpy(b->b + b->len, src, len);
	b->len += len;
	if (flags & BF_APPEND_NUL)
		b->b[b->len] = '\0';
	return len;
}

int
buf_make_immutable(buf_t *buf, int newofs)
{
	if (newofs == -1)
		newofs = buf->len;
	if (newofs < buf->sofs) {
		debug(85, 5) ("buf_make_immutable: %p: already seen %d bytes as immutable!!\n", buf, newofs);
		return 1;
	}
	debug(85, 5) ("buf_make_immutable: %p: immutable amount now %d bytes\n", buf, newofs);
	buf->sofs = newofs;
	return 1;
}


int
buf_truncate(buf_t *b, int newlen, buf_flags_t flags)
{
	if (newlen > b->len)
		return 0;
	b->len = newlen;
	if (flags & BF_APPEND_NUL)
		b->b[b->len] = '\0';
	return 1;
}

char *
buf_dup_cbuf(buf_t *b)
{
	char *c;

	c = malloc(buf_len(b) + 1);
	memcpy(c, buf_buf(b), buf_len(b));
	c[buf_len(b)] = '\0';
	return c;
}

