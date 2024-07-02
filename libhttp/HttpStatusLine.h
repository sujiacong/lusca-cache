#ifndef	__LIBHTTP_HTTPSTATUSLINE_H__
#define	__LIBHTTP_HTTPSTATUSLINE_H__

typedef enum {
    HTTP_STATUS_NONE = 0,
    HTTP_CONTINUE = 100,
    HTTP_SWITCHING_PROTOCOLS = 101,
    HTTP_PROCESSING = 102,      /* RFC2518 section 10.1 */
    HTTP_OK = 200,
    HTTP_CREATED = 201,
    HTTP_ACCEPTED = 202,
    HTTP_NON_AUTHORITATIVE_INFORMATION = 203,
    HTTP_NO_CONTENT = 204,
    HTTP_RESET_CONTENT = 205,
    HTTP_PARTIAL_CONTENT = 206,
    HTTP_MULTI_STATUS = 207,    /* RFC2518 section 10.2 */
    HTTP_MULTIPLE_CHOICES = 300,
    HTTP_MOVED_PERMANENTLY = 301,
    HTTP_MOVED_TEMPORARILY = 302,
    HTTP_SEE_OTHER = 303,
    HTTP_NOT_MODIFIED = 304,
    HTTP_USE_PROXY = 305,
    HTTP_TEMPORARY_REDIRECT = 307,
    HTTP_BAD_REQUEST = 400,
    HTTP_UNAUTHORIZED = 401,
    HTTP_PAYMENT_REQUIRED = 402,
    HTTP_FORBIDDEN = 403,
    HTTP_NOT_FOUND = 404,
    HTTP_METHOD_NOT_ALLOWED = 405,
    HTTP_NOT_ACCEPTABLE = 406,
    HTTP_PROXY_AUTHENTICATION_REQUIRED = 407,
    HTTP_REQUEST_TIMEOUT = 408,
    HTTP_CONFLICT = 409,
    HTTP_GONE = 410,
    HTTP_LENGTH_REQUIRED = 411,
    HTTP_PRECONDITION_FAILED = 412,
    HTTP_REQUEST_ENTITY_TOO_LARGE = 413,
    HTTP_REQUEST_URI_TOO_LONG = 414,
    HTTP_UNSUPPORTED_MEDIA_TYPE = 415,
    HTTP_EXPECTATION_FAILED = 417,
    HTTP_UNPROCESSABLE_ENTITY = 422,    /* RFC2518 section 10.3 */
    HTTP_LOCKED = 423,          /* RFC2518 section 10.4 */
    HTTP_FAILED_DEPENDENCY = 424,       /* RFC2518 section 10.5 */
    HTTP_INTERNAL_SERVER_ERROR = 500,
    HTTP_NOT_IMPLEMENTED = 501,
    HTTP_BAD_GATEWAY = 502,
    HTTP_SERVICE_UNAVAILABLE = 503,
    HTTP_GATEWAY_TIMEOUT = 504,
    HTTP_HTTP_VERSION_NOT_SUPPORTED = 505,
    HTTP_INSUFFICIENT_STORAGE = 507,    /* RFC2518 section 10.6 */

    HTTP_INVALID_HEADER = 1001,		/* Squid header parsing error */
    HTTP_HEADER_TOO_LARGE = 1002	/* Header too large to process */
} http_status;

/* http status line */
struct _HttpStatusLine {
    /* public, read only */
    http_version_t version;
    const char *reason;         /* points to a _constant_ string (default or supplied), never free()d */
    http_status status;
};

typedef struct _HttpStatusLine HttpStatusLine;

extern const char *HttpStatusLineFormat;

/* Http Status Line */
/* init/clean */
extern void httpStatusLineInit(HttpStatusLine * sline);
extern void httpStatusLineClean(HttpStatusLine * sline);
/* set/get values */
extern void httpStatusLineSet(HttpStatusLine * sline, http_version_t version,
    http_status status, const char *reason);
extern const char *httpStatusLineReason(const HttpStatusLine * sline);
/* parse/pack */
/* parse a 0-terminating buffer and fill internal structires; returns true on success */
extern int httpStatusLineParse(HttpStatusLine * sline, const char *start,
    const char *end);
extern const char *httpStatusString(http_status status);



#endif
