#ifndef	__LIBHTTP_HTTPHEADERSTATS_H__
#define	__LIBHTTP_HTTPHEADERSTATS_H__

/* per header statistics */
struct _HttpHeaderStat {
    const char *label;
    HttpHeaderMask *owner_mask;

    StatHist hdrUCountDistr;
    StatHist fieldTypeDistr;
    StatHist ccTypeDistr;

    int parsedCount;
    int ccParsedCount;
    int destroyedCount;
    int busyDestroyedCount;
};
typedef struct _HttpHeaderStat HttpHeaderStat;

extern HttpHeaderStat HttpHeaderStats[];
extern int HttpHeaderStatCount;

extern void httpHeaderStatInit(HttpHeaderStat * hs, const char *label);

#endif
