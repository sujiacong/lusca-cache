#ifndef	__LIBHTTP_HTTP_HDR_CC__
#define	__LIBHTTP_HTTP_HDR_CC__

/* http cache control header field */ 
struct _HttpHdrCc { 
    int mask;
    int max_age;
    int s_maxage;
    int max_stale;
    int stale_while_revalidate; 
    int stale_if_error;
    String other;
};
typedef struct _HttpHdrCc HttpHdrCc; 

extern HttpHeaderFieldInfo *CcFieldsInfo;
extern const HttpHeaderFieldAttrs CcAttrs[CC_ENUM_END];

extern void httpHdrCcInitModule(void);
extern void httpHdrCcCleanModule(void);
extern HttpHdrCc *httpHdrCcCreate(void);
extern HttpHdrCc *httpHdrCcParseCreate(const String * str);
extern void httpHdrCcDestroy(HttpHdrCc * cc);
extern HttpHdrCc *httpHdrCcDup(const HttpHdrCc * cc);
extern void httpHdrCcJoinWith(HttpHdrCc * cc, const HttpHdrCc * new_cc);
extern void httpHdrCcSetMaxAge(HttpHdrCc * cc, int max_age);
extern void httpHdrCcSetSMaxAge(HttpHdrCc * cc, int s_maxage);
extern void httpHdrCcUpdateStats(const HttpHdrCc * cc, StatHist * hist);
extern HttpHdrCc *httpHeaderGetCc(const HttpHeader * hdr);

#endif
