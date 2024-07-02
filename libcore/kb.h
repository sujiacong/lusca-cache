#ifndef	__LIBCORE_KB_H__
#define	__LIBCORE_KB_H__


/*
 * XXX this strictly shouldn't be here!
 */

#if SIZEOF_INT64_T > SIZEOF_LONG && HAVE_STRTOLL
typedef int64_t squid_off_t;
#define SIZEOF_SQUID_OFF_T SIZEOF_INT64_T
#define PRINTF_OFF_T PRId64
#define strto_off_t (int64_t)strtoll
#else
typedef long squid_off_t;
#define SIZEOF_SQUID_OFF_T SIZEOF_LONG
#define PRINTF_OFF_T "ld"
#define strto_off_t strtol
#endif


typedef struct {
    squid_off_t bytes;
    squid_off_t kb;
} kb_t;

extern void kb_incr(kb_t *, squid_off_t);


#endif
