#ifndef	__LIBHTTP_HTTPHEADERTYPE_H__
#define	__LIBHTTP_HTTPHEADERTYPE_H__

/* recognized or "known" header fields; @?@ add more! */
typedef enum {
    HDR_UNKNOWN = -1,
    HDR_ACCEPT = 0,                     /**< RFC 7231 */
    HDR_ACCEPT_CHARSET,                 /**< RFC 7231 */
    HDR_ACCEPT_ENCODING,                /**< RFC 7231 */
    /*HDR_ACCEPT_FEATURES,*/            /* RFC 2295 */
    HDR_ACCEPT_LANGUAGE,                /**< RFC 7231 */
    HDR_ACCEPT_RANGES,                  /**< RFC 7233 */
    HDR_AGE,                            /**< RFC 7234 */
    HDR_ALLOW,                          /**< RFC 7231 */
    HDR_ALTERNATE_PROTOCOL,             /**< GFE custom header we may have to erase */
    HDR_AUTHENTICATION_INFO,            /**< RFC 2617 */
    HDR_AUTHORIZATION,                  /**< RFC 7235, 4559 */
    HDR_CACHE_CONTROL,                  /**< RFC 7234 */
    HDR_CONNECTION,                     /**< RFC 7230 */
    HDR_CONTENT_BASE,                   /**< obsoleted RFC 2068 */
    HDR_CONTENT_DISPOSITION,            /**< RFC 2183, 6266 */
    HDR_CONTENT_ENCODING,               /**< RFC 7231 */
    HDR_CONTENT_LANGUAGE,               /**< RFC 7231 */
    HDR_CONTENT_LENGTH,                 /**< RFC 7230 */
    HDR_CONTENT_LOCATION,               /**< RFC 7231 */
    HDR_CONTENT_MD5,                    /**< deprecated, RFC 2616 */
    HDR_CONTENT_RANGE,                  /**< RFC 7233 */
    HDR_CONTENT_TYPE,                   /**< RFC 7231 */
    HDR_COOKIE,                         /**< RFC 6265 header we may need to erase */
    HDR_COOKIE2,                        /**< obsolete RFC 2965 header we may need to erase */
    HDR_DATE,                           /**< RFC 7231 */
    /*HDR_DAV,*/                        /* RFC 2518 */
    /*HDR_DEPTH,*/                      /* RFC 2518 */
    /*HDR_DERIVED_FROM,*/               /* deprecated RFC 2068 */
    /*HDR_DESTINATION,*/                /* RFC 2518 */
    HDR_ETAG,                           /**< RFC 7232 */
    HDR_EXPECT,                         /**< RFC 7231 */
    HDR_EXPIRES,                        /**< RFC 7234 */
    HDR_FORWARDED,                      /**< RFC 7239 */
    HDR_FROM,                           /**< RFC 7231 */
    HDR_HOST,                           /**< RFC 7230 */
    HDR_HTTP2_SETTINGS,                 /**< RFC 7540 */
    /*HDR_IF,*/                         /* RFC 2518 */
    HDR_IF_MATCH,                       /**< RFC 7232 */
    HDR_IF_MODIFIED_SINCE,              /**< RFC 7232 */
    HDR_IF_NONE_MATCH,                  /**< RFC 7232 */
    HDR_IF_RANGE,                       /**< RFC 7233 */
    HDR_IF_UNMODIFIED_SINCE,            /**< RFC 7232 */
    HDR_KEEP_ALIVE,                     /**< obsoleted RFC 2068 header we may need to erase */
    HDR_KEY,                            /**< experimental RFC Draft draft-fielding-http-key-02 */
    HDR_LAST_MODIFIED,                  /**< RFC 7232 */
    HDR_LINK,                           /**< RFC 5988 */
    HDR_LOCATION,                       /**< RFC 7231 */
    /*HDR_LOCK_TOKEN,*/                 /* RFC 2518 */
    HDR_MAX_FORWARDS,                   /**< RFC 7231 */
    HDR_MIME_VERSION,                   /**< RFC 2045, 7231 */
    HDR_NEGOTIATE,                      /**< experimental RFC 2295. Why only this one from 2295? */
    /*HDR_OVERWRITE,*/                  /* RFC 2518 */
    HDR_ORIGIN,                         /* CORS Draft specification (see http://www.w3.org/TR/cors/) */
    HDR_PRAGMA,                         /**< RFC 7234 */
    HDR_PROXY_AUTHENTICATE,             /**< RFC 7235 */
    HDR_PROXY_AUTHENTICATION_INFO,      /**< RFC 2617 */
    HDR_PROXY_AUTHORIZATION,            /**< RFC 7235 */
    HDR_PROXY_CONNECTION,               /**< obsolete Netscape header we may need to erase. */
    HDR_PROXY_SUPPORT,                  /**< RFC 4559 */
    HDR_PUBLIC,                         /**<  RFC 2068 */
    HDR_RANGE,                          /**< RFC 7233 */
    HDR_REFERER,                        /**< RFC 7231 */
    HDR_REQUEST_RANGE,                  /**< some clients use this, sigh */
    HDR_RETRY_AFTER,                    /**< RFC 7231 */
    HDR_SERVER,                         /**< RFC 7231 */
    HDR_SET_COOKIE,                     /**< RFC 6265 header we may need to erase */
    HDR_SET_COOKIE2,                    /**< obsoleted RFC 2965 header we may need to erase */
    /*HDR_STATUS_URI,*/                 /* RFC 2518 */
    /*HDR_TCN,*/                        /* experimental RFC 2295 */
    HDR_TE,                             /**< RFC 7230 */
    /*HDR_TIMEOUT,*/                    /* RFC 2518 */
    HDR_TITLE,                          /* obsolete draft suggested header */
    HDR_TRAILER,                        /**< RFC 7230 */
    HDR_TRANSFER_ENCODING,              /**< RFC 7230 */
    HDR_TRANSLATE,                      /**< IIS custom header we may need to erase */
    HDR_UNLESS_MODIFIED_SINCE,          /**< IIS custom header we may need to erase */
    HDR_UPGRADE,                        /**< RFC 7230 */
    HDR_USER_AGENT,                     /**< RFC 7231 */
    /*HDR_VARIANT_VARY,*/               /* experimental RFC 2295 */
    HDR_VARY,                           /**< RFC 7231 */
    HDR_VIA,                            /**< RFC 7230 */
    HDR_WARNING,                        /**< RFC 7234 */
    HDR_WWW_AUTHENTICATE,               /**< RFC 7235, 4559 */
    HDR_X_CACHE,                        /**< Squid custom header */
    HDR_X_CACHE_LOOKUP,                 /**< Squid custom header. temporary hack that became de-facto. TODO remove */
    HDR_X_FORWARDED_FOR,                /**< obsolete Squid custom header, RFC 7239 */
    HDR_X_REQUEST_URI,                  /**< Squid custom header appended if ADD_X_REQUEST_URI is defined */
    HDR_X_SQUID_ERROR,                  /**< Squid custom header on generated error responses */
#if X_ACCELERATOR_VARY
    HDR_X_ACCELERATOR_VARY,             /**< obsolete Squid custom header. */
#endif
#if USE_ADAPTATION
    HDR_X_NEXT_SERVICES,                /**< Squid custom ICAP header */
#endif
    HDR_SURROGATE_CAPABILITY,           /**< Edge Side Includes (ESI) header */
    HDR_SURROGATE_CONTROL,              /**< Edge Side Includes (ESI) header */
    HDR_FRONT_END_HTTPS,                /**< MS Exchange custom header we may have to add */
    HDR_FTP_COMMAND,                    /**< Internal header for FTP command */
    HDR_FTP_ARGUMENTS,                  /**< Internal header for FTP command arguments */
    HDR_FTP_PRE,                        /**< Internal header containing leading FTP control response lines */
    HDR_FTP_STATUS,                     /**< Internal header for FTP reply status */
    HDR_FTP_REASON,                     /**< Internal header for FTP reply reason */
	HDR_X_ERROR_STATUS,					/**< errormap, received HTTP status line */
	HDR_X_ERROR_URL,					/**< errormap, requested URL */
	HDR_X_HTTP09_FIRST_LINE,    		/**< internal, first line of HTTP/0.9 response */
    HDR_OTHER,                          /**< internal tag value for "unknown" headers */
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
    ftInvalid = HDR_ENUM_END,   /**< to catch nasty errors with hdr_id<->fld_type clashes */
    ftInt,
    ftInt64,
    ftStr,
    ftDate_1123,
    ftETag,
    ftPCc,
    ftPContRange,
    ftPRange,
    ftPSc,
    ftDate_1123_or_ETag
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
