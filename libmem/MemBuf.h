#ifndef	__LIBMEM_MEMBUF_H__
#define	__LIBMEM_MEMBUF_H__

/* to initialize static variables (see also MemBufNull) */
#define MemBufNULL { NULL, 0, 0, 0, 0 }

/* in case we want to change it later */
typedef int mb_size_t;

/* auto-growing memory-resident buffer with printf interface */
/* note: when updating this struct, update MemBufNULL #define */
struct _MemBuf {
    /* public, read-only */
    char *buf;
    mb_size_t size;             /* used space, does not count 0-terminator */

    /* private, stay away; use interface function instead */
    mb_size_t max_capacity;     /* when grows: assert(new_capacity <= max_capacity) */
    mb_size_t capacity;         /* allocated space */
    unsigned stolen:1;          /* the buffer has been stolen for use by someone else */
};
typedef struct _MemBuf MemBuf;

extern const MemBuf MemBufNull; /* MemBufNULL */

/* MemBuf */
/* init with specific sizes */
extern void memBufInit(MemBuf * mb, mb_size_t szInit, mb_size_t szMax);
/* init with defaults */
extern void memBufDefInit(MemBuf * mb);
/* cleans mb; last function to call if you do not give .buf away */
extern void memBufClean(MemBuf * mb);
/* resets mb preserving (or initializing if needed) memory buffer */
extern void memBufReset(MemBuf * mb);
/* unfirtunate hack to test if the buffer has been Init()ialized */
extern int memBufIsNull(MemBuf * mb);
/* calls memcpy, appends exactly size bytes, extends buffer if needed */
extern void memBufAppend(MemBuf * mb, const void *buf, int size);
/* calls snprintf, extends buffer if needed */
#if STDC_HEADERS
extern void
memBufPrintf(MemBuf * mb, const char *fmt,...) PRINTF_FORMAT_ARG2;
#else
extern void memBufPrintf();
#endif
/* vprintf for other printf()'s to use */
extern void memBufVPrintf(MemBuf * mb, const char *fmt, va_list ap);
/* returns free() function to be used, _freezes_ the object! */
extern FREE *memBufFreeFunc(MemBuf * mb);
/* puts report on MemBuf _module_ usage into mb */
extern void memBufReport(MemBuf * mb);
extern void memBufGrow(MemBuf * mb, mb_size_t min_cap);

#endif
