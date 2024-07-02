#ifndef	__LIBMEM_BUF_H__
#define	__LIBMEM_BUF_H__

#define	BUF_TRACK_BUFS		0

struct _buf {
	char *b;
	int len;
	int size;
	int sofs;	/* how much of the buffer can't be changed */
#if	BUF_TRACK_BUFS
	dlink_node node;
#endif
	struct {
		char isactive:1;
		char isfinal:1;
		char isfreed:1;
		char isconst:1;
	} flags;
	struct {
		const char *file;
		int line;
	} create, ref, deref;
	int nref;
};

typedef enum {
	BF_NONE,
	BF_APPEND_NUL
} buf_flags_t;

typedef struct _buf buf_t;

#define		buf_create()		buf_create_int(__FILE__, __LINE__)
#define		buf_create_size(a)		buf_create_size_int(a, __FILE__, __LINE__)
#define		buf_create_const(a, b)		buf_create_const_int(a, b, __FILE__, __LINE__)
extern		void buf_init(void);
extern		buf_t * buf_create_int(const char *file, int line);
extern		buf_t * buf_create_size_int(int len, const char *file, int line);
extern		buf_t * buf_create_const_int(const void *data, size_t len, const char *file, int line);
extern		buf_t * buf_ref(buf_t *buf);
extern		buf_t * buf_deref(buf_t *buf);
extern		void buf_free(buf_t *buf);
extern		int buf_fill(buf_t *buf, int fd, int dogrow);
extern		int buf_make_immutable(buf_t *buf, int offset);
extern		int buf_append(buf_t *buf, const void *src, size_t len, buf_flags_t flags);
extern		int buf_grow_to_min_free(buf_t *b, int minfree);
extern		int buf_changesize(buf_t *buf, int newsize);
extern		int buf_truncate(buf_t *buf, int len, buf_flags_t flags);
extern		char * buf_dup_cbuf(buf_t *buf);


static inline int buf_len(const buf_t *buf) { return buf->len; }

/* All use of this should be replaced by inline methods in C++.
 * has to be a define in C due to the lack of typing (const vs no-const)
 */
#define		buf_buf(buf)	((buf)->b)
#define		buf_len(buf)	((buf)->len)
#define		buf_capacity(buf)	((buf)->size)
#define		buf_refcnt(buf)	((buf)->nref)
#define		buf_remainder(buf)	( (buf)->size - (buf)->len )

static inline int
buf_get_chr(const buf_t *buf, int offset) {
	if (offset >= buf->len)
		return -1;
	return (unsigned char)buf_buf(buf)[offset];
}

static inline int
buf_put_chr(buf_t *buf, int offset, char val)
{
	if (offset >= buf->size)
		return 0;
	buf_buf(buf)[offset] = val;
	return 1;
}

static inline int
buf_isfull(buf_t *b) { return (b->size == b->len); }

#if	BUF_TRACK_BUFS
extern dlink_list buf_active_list;
#endif

#endif
