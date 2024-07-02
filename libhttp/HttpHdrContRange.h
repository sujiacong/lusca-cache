#ifndef	__LIBHTTP_HTTP_HDR_CONT_RANGE_H__
#define	__LIBHTTP_HTTP_HDR_CONT_RANGE_H__


/* http content-range header field */
struct _HttpHdrContRange {
    HttpHdrRangeSpec spec;
    squid_off_t elength;        /* entity length, not content length */
};  
typedef struct _HttpHdrContRange HttpHdrContRange;

/* Http Content Range Header Field */
extern HttpHdrContRange *httpHdrContRangeCreate(void);
extern HttpHdrContRange *httpHdrContRangeParseCreate(const char *crange_spec);
/* returns true if range is valid; inits HttpHdrContRange */
extern int httpHdrContRangeParseInit(HttpHdrContRange * crange, const char *crange_spec);
extern void httpHdrContRangeDestroy(HttpHdrContRange * crange); 
extern HttpHdrContRange *httpHdrContRangeDup(const HttpHdrContRange * crange);
extern void httpHdrContRangeSet(HttpHdrContRange *, HttpHdrRangeSpec, squid_off_t);
extern HttpHdrContRange *httpHeaderGetContRange(const HttpHeader * hdr);


#endif
