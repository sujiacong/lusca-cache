#ifndef	__LIBHTTP_HTTP_MSG_H__
#define	__LIBHTTP_HTTP_MSG_H__

/* parse state of HttpReply or HttpRequest */
typedef enum {
    psReadyToParseStartLine = 0,
    psReadyToParseHeaders,
    psParsed,
    psError
} HttpMsgParseState;

struct _HttpMsgBuf {
    const char *buf;
    size_t size;
    /* offset of first/last byte of headers */
    int h_start, h_end, h_len;
    /* offset of first/last byte of request, including any padding */
    int req_start, req_end, r_len;
    int m_start, m_end, m_len;
    int u_start, u_end, u_len;
    int v_start, v_end, v_len;
    int v_maj, v_min;
};
typedef struct _HttpMsgBuf HttpMsgBuf;

extern int httpMsgIsolateHeaders(const char **parse_start, int l, const char **blk_start, const char **blk_end);
extern int httpMsgIsPersistent(http_version_t http_ver, const HttpHeader * hdr);
extern void HttpMsgBufInit(HttpMsgBuf * hmsg, const char *buf, size_t size);
extern void httpMsgBufDone(HttpMsgBuf * hmsg);
extern int httpMsgParseRequestLine(HttpMsgBuf * hmsg);
extern int httpMsgFindHeadersEnd(HttpMsgBuf * hmsg);

#endif
