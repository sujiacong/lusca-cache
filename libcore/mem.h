#ifndef	__LIBCORE_MEM_H__
#define	__LIBCORE_MEM_H__

/*
 * These functions implement traditional malloc-with-failure
 * wrapping so code can easily hook into the malloc/free path
 * without needing to override the symbols (which may occur
 * for other reasons, eg including the google malloc library
 * at runtime.)
 *
 * The code in lib/ (xmalloc, etc) guarantees a valid pointer on
 * return which makes it difficult to try and code for transient
 * out of memory failures.
 */
static inline void *
xxmalloc(size_t sz)
{
	return malloc(sz);
}

static inline void *
xxcalloc(size_t count, size_t sz)
{
	return calloc(count, sz);
}

static inline void *
xxrealloc(void *ptr, size_t sz)
{
	return realloc(ptr, sz);
}

#define	xxfree(p)	do { if (p) free(p); (p) = NULL; } while (0)

#endif
