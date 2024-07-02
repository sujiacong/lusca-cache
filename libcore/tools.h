#ifndef	__LIBCORE_TOOLS_H__
#define	__LIBCORE_TOOLS_H__

#define MB ((size_t)1024*1024)
extern double toMB(size_t size);
extern size_t toKB(size_t size);

#define safe_free(x)    if (x) { xxfree(x); x = NULL; }

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* How big is the region of memory between two char pointers? */
static inline int charBufferSize(const char *start, const char *end) { return (end - start + 1); }

#define XMIN(x,y) ((x)<(y)? (x) : (y))
#define XMAX(x,y) ((x)>(y)? (x) : (y))
#define EBIT_SET(flag, bit)     ((void)((flag) |= ((1L<<(bit)))))
#define EBIT_CLR(flag, bit)     ((void)((flag) &= ~((1L<<(bit)))))
#define EBIT_TEST(flag, bit)    ((flag) & ((1L<<(bit))))

extern struct timeval current_time;
extern double current_dtime;
extern time_t squid_curtime;    /* 0 */

extern time_t getCurrentTime(void);

extern void libcore_fatalf(const char *fmt, ...);
typedef void FATALF_FUNC(const char *fmt, va_list args); 
extern void libcore_set_fatalf(FATALF_FUNC *f);

extern int xusleep(unsigned int usec);

#if LEAK_CHECK_MODE
#define LOCAL_ARRAY(type,name,size) \
        static type *local_##name=NULL; \
        type *name = local_##name ? local_##name : \
                ( local_##name = (type *)xcalloc(size, sizeof(type)) )
#else
#define LOCAL_ARRAY(type,name,size) static type name[size]
#endif

/* bit opearations on a char[] mask of unlimited length */
#define CBIT_BIT(bit)           (1<<((bit)%8))
#define CBIT_BIN(mask, bit)     (mask)[(bit)>>3]
#define CBIT_SET(mask, bit)     ((void)(CBIT_BIN(mask, bit) |= CBIT_BIT(bit)))
#define CBIT_CLR(mask, bit)     ((void)(CBIT_BIN(mask, bit) &= ~CBIT_BIT(bit)))
#define CBIT_TEST(mask, bit)    ((CBIT_BIN(mask, bit) & CBIT_BIT(bit)) != 0)

/* handy to determine the #elements in a static array */
#define countof(arr) (sizeof(arr)/sizeof(*arr))

extern const char *w_space;

extern int stringHasWhitespace(const char *);

extern int intAverage(int, int, int, int);
extern double doubleAverage(double, double, int, int);

extern void doBacktrace(void);

/* XXX this probably shouldn't be in here! */
/*
 * ISO C99 Standard printf() macros for 64 bit integers
 * On some 64 bit platform, HP Tru64 is one, for printf must be used
 * "%lx" instead of "%llx"
 */
#ifndef PRId64
#ifdef _SQUID_MSWIN_            /* Windows native port using MSVCRT */
#define PRId64 "I64d"
#elif SIZEOF_INT64_T > SIZEOF_LONG
#define PRId64 "lld"
#else
#define PRId64 "ld"
#endif
#endif

#ifndef PRIu64
#ifdef _SQUID_MSWIN_            /* Windows native port using MSVCRT */
#define PRIu64 "I64u"
#elif SIZEOF_INT64_T > SIZEOF_LONG
#define PRIu64 "llu"
#else
#define PRIu64 "lu"
#endif
#endif

#define SQUID_MAXPATHLEN 256
#ifndef MAXPATHLEN
#define MAXPATHLEN SQUID_MAXPATHLEN
#endif

extern int percent(int, int);
extern double dpercent(double, double);
extern double uint64_percent(u_int64_t, u_int64_t);

#endif
