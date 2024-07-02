#ifndef	__LIBHTTP_HTTP_BODY_H__
#define	__LIBHTTP_HTTP_BODY_H__

/*
 * Note: HttpBody is used only for messages with a small content that is
 * known a priory (e.g., error messages).
 */
struct _HttpBody {
    /* private */
    MemBuf mb;
};
typedef struct _HttpBody HttpBody;

extern void httpBodyInit(HttpBody * body);
extern void httpBodyClean(HttpBody * body);
/* get body ptr (always use this) */
extern const char *httpBodyPtr(const HttpBody * body);
/* set body, does not clone mb so you should not reuse it */
extern void httpBodySet(HttpBody * body, MemBuf * mb);

#endif
