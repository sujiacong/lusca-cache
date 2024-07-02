#ifndef	__LIBHTTP_HTTP_REPLY_H__
#define	__LIBHTTP_HTTP_REPLY_H__

struct _HttpReply {
    /* unsupported, writable, may disappear/change in the future */
    int hdr_sz;                 /* sums _stored_ status-line, headers, and <CRLF> */
 
    /* public, readable; never update these or their .hdr equivalents directly */
    squid_off_t content_length;
    time_t date;
    time_t last_modified;
    time_t expires; 
    String content_type;
    HttpHdrCc *cache_control;
    HttpHdrContRange *content_range;
    short int keep_alive;

    /* public, readable */
    HttpMsgParseState pstate;   /* the current parsing state */

    /* public, writable, but use httpReply* interfaces when possible */
    HttpStatusLine sline;
    HttpHeader header;
    HttpBody body;              /* for small constant memory-resident text bodies only */
};

typedef struct _HttpReply HttpReply;
typedef struct _HttpReply http_reply;

extern MemPool * pool_http_reply;

#endif
