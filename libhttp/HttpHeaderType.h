#ifndef	__LIBHTTP_HTTPHEADERTYPE_H__
#define	__LIBHTTP_HTTPHEADERTYPE_H__

/* recognized or "known" header fields; @?@ add more! */
typedef enum {
    HDR_UNKNOWN = -1,
    HDR_ACCEPT = 0,
    HDR_ACCEPT_CHARSET,
    HDR_ACCEPT_ENCODING,
    HDR_ACCEPT_LANGUAGE,
    HDR_ACCEPT_RANGES,
    HDR_AGE,
    HDR_ALLOW,
    HDR_AUTHORIZATION,
    HDR_CACHE_CONTROL,
    HDR_CONNECTION,
    HDR_CONTENT_BASE,
    HDR_CONTENT_DISPOSITION,
    HDR_CONTENT_ENCODING,
    HDR_CONTENT_LANGUAGE,
    HDR_CONTENT_LENGTH,
    HDR_CONTENT_LOCATION,
    HDR_CONTENT_MD5,
    HDR_CONTENT_RANGE,
    HDR_CONTENT_TYPE,
    HDR_TE,
    HDR_TRANSFER_ENCODING,
    HDR_TRAILER,
    HDR_COOKIE,
    HDR_DATE,
    HDR_ETAG,
    HDR_EXPIRES,
    HDR_FROM,
    HDR_HOST,
    HDR_IF_MATCH,
    HDR_IF_MODIFIED_SINCE,
    HDR_IF_NONE_MATCH,
    HDR_IF_RANGE,
    HDR_LAST_MODIFIED,
    HDR_LINK,
    HDR_LOCATION,
    HDR_MAX_FORWARDS,
    HDR_MIME_VERSION,
    HDR_PRAGMA,
    HDR_PROXY_AUTHENTICATE,
    HDR_PROXY_AUTHENTICATION_INFO,
    HDR_PROXY_AUTHORIZATION,
    HDR_PROXY_CONNECTION,
    HDR_PUBLIC,
    HDR_RANGE,
    HDR_REQUEST_RANGE,          /* some clients use this, sigh */
    HDR_REFERER,
    HDR_RETRY_AFTER,
    HDR_SERVER,
    HDR_SET_COOKIE,
    HDR_UPGRADE,
    HDR_USER_AGENT,
    HDR_VARY,
    HDR_VIA,
    HDR_EXPECT,
    HDR_WARNING,
    HDR_WWW_AUTHENTICATE,
    HDR_AUTHENTICATION_INFO,
    HDR_X_CACHE,
    HDR_X_CACHE_LOOKUP,         /* tmp hack, remove later */
    HDR_X_FORWARDED_FOR,
    HDR_X_REQUEST_URI,          /* appended if ADD_X_REQUEST_URI is #defined */
    HDR_X_SQUID_ERROR,
    HDR_NEGOTIATE,
#if X_ACCELERATOR_VARY
    HDR_X_ACCELERATOR_VARY,
#endif
    HDR_X_ERROR_URL,            /* errormap, requested URL */
    HDR_X_ERROR_STATUS,         /* errormap, received HTTP status line */
    HDR_X_HTTP09_FIRST_LINE,	/* internal, first line of HTTP/0.9 response */
    HDR_FRONT_END_HTTPS,
    HDR_PROXY_SUPPORT,
    HDR_KEEP_ALIVE,
    HDR_OTHER,
    HDR_ENUM_END
} http_hdr_type;

typedef enum {
    CC_PUBLIC,
    CC_PRIVATE,
    CC_NO_CACHE,
    CC_NO_STORE,
    CC_NO_TRANSFORM,
    CC_MUST_REVALIDATE,
    CC_PROXY_REVALIDATE,
    CC_MAX_AGE,
    CC_S_MAXAGE,
    CC_MAX_STALE,
    CC_ONLY_IF_CACHED,
    CC_STALE_WHILE_REVALIDATE,
    CC_STALE_IF_ERROR,
    CC_OTHER,
    CC_ENUM_END
} http_hdr_cc_type;

/* possible owners of http header */
typedef enum {
    hoNone,
#if USE_HTCP
    hoHtcpReply,
#endif
    hoRequest,
    hoReply
} http_hdr_owner_type;

/* possible types for http header fields */
typedef enum {
    ftInvalid = HDR_ENUM_END,   /* to catch nasty errors with hdr_id<->fld_type clashes */
    ftInt,
    ftStr,
    ftDate_1123,
    ftETag,
    ftPCc,
    ftPContRange,
    ftPRange,
    ftDate_1123_or_ETag,
    ftSize
} field_type;

/* constant attributes of http header fields */
struct _HttpHeaderFieldAttrs {
    const char *name;
    http_hdr_type id;
    field_type type;
};
typedef struct _HttpHeaderFieldAttrs HttpHeaderFieldAttrs;

extern const HttpHeaderFieldAttrs HeadersAttrs[];
extern const int HeadersAttrsCount;

#endif
