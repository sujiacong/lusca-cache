#ifndef	__LIBHTTP_HTTPHEADERFIELDSTAT_H__
#define	__LIBHTTP_HTTPHEADERFIELDSTAT_H__

/* per field statistics */
struct _HttpHeaderFieldStat {
    int aliveCount;             /* created but not destroyed (count) */
    int seenCount;              /* #fields we've seen */
    int parsCount;              /* #parsing attempts */
    int errCount;               /* #pasring errors */
    int repCount;               /* #repetitons */
};
typedef struct _HttpHeaderFieldStat HttpHeaderFieldStat;

#endif
